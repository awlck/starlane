#include "location.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include <pugixml.hpp>

#include "../game.h"
#include "character.h"
#include "description.h"
#include "event.h"
#include "group.h"
#include "restriction.h"

namespace Starlane {

Location *Location::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Location;
	auto theGame = Game::Get();
	result->locationName = theGame->CreateDescFromXML(xmlNode.child("ShortDescription"));
	result->description = theGame->CreateDescFromXML(xmlNode.child("LongDescription"));
	result->MakeCommonValues(xmlNode);
	result->relation = HoldingType::Hidden;  // not really, but it also doesn't matter for locations at all.

	{
		auto exits = std::make_shared<std::unordered_map<std::string, ExitSpec>>();
		for (const auto &m: xmlNode.children("Movement")) {
			std::string direction = m.child_value("Direction");
			std::string destination = m.child_value("Destination");
			RestrRef restrs = 0;
			const auto &r = m.child("Restrictions");
			if (r.type() != pugi::node_null)
				restrs = theGame->CreateRestrictionsFromXML(r);
			(*exits)[direction] = { destination, restrs };
		}
		result->exits = std::move(exits);
	}

	result->MakeMatchExpr();
	return result;
}

std::string Location::GetDisplayName([[maybe_unused]] bool defArt) const {
	// A location's name is ADRIFT's "ShortLocationDescription" property, so a location group can
	// supply one for all its members ("At the Bottom of the Ocean" for four ocean locations that
	// each carry only a bookkeeping name of their own). A group's value wins, as it does for
	// every other property.
	if (const Group *grp = GetGroupWithProp("ShortLocationDescription"))
		return Game::Get()->GetDescription((DescrRef) grp->GetIntProp("ShortLocationDescription"))->Build();
	if (locationName == (DescrRef) 0)
		return "(BUG: Location without a name.)";
	return Game::Get()->GetDescription(locationName)->Build();
}

GameObj *Location::Clone() const {
	return new Location(*this);
}

// Separate a piece of text being appended to a description from what came before,
// unless the text already ends in whitespace. (The equivalent of pSpace in the
// original implementation, which uses the two-spaces-after-a-sentence convention.)
static void PadForAppend(std::string &s) {
	if (s.empty()) return;
	char last = s.back();
	if (last != ' ' && last != '\n' && last != '\t')
		s += "  ";
}

// Join names the way the original runner lists them: "A", "A and B", "A, B and C".
static std::string JoinNames(const std::vector<std::string> &names) {
	std::string result;
	for (size_t i = 0; i < names.size(); i++) {
		if (i > 0)
			result += i == names.size() - 1 ? " and " : ", ";
		result += names[i];
	}
	return result;
}

static void ReplaceAll(std::string &str, const std::string &from, const std::string &to) {
	if (from.empty()) return;
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.size(), to);
		pos += to.size();
	}
}

// How many bytes the UTF-8 character starting with this byte occupies. Invalid input is taken a
// byte at a time, which keeps us moving forward rather than reading off the end.
static size_t Utf8CharLen(unsigned char c) {
	if (c < 0x80) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;
}

// Lowercase `s` via the frontend, recording for each byte of the result the byte of `s` it came
// from (plus a trailing entry for the end, so that a match reaching the end can be measured).
// One character at a time, because a whole-string fold leaves no way back: case folding can
// change a string's length, so an offset into the folded text locates nothing in the original.
// The cost is the handful of foldings that depend on surrounding characters -- a Greek final
// sigma -- which is a far smaller error than mangling the text.
static std::string FoldRecordingOrigins(const std::string &s, std::vector<size_t> &origin) {
	std::string folded;
	origin.clear();
	for (size_t i = 0; i < s.size(); ) {
		const size_t n = std::min(Utf8CharLen((unsigned char) s[i]), s.size() - i);
		const std::string one = frontend->StrToLowerCase(s.substr(i, n));
		folded += one;
		origin.insert(origin.end(), one.size(), i);
		i += n;
	}
	origin.push_back(s.size());
	return folded;
}

// Replace only the first case-insensitive occurrence of `from`, matched literally.
// (The original's ReplaceIgnoreCase escapes the needle's regex metacharacters and caps
//  its replacement count at one, so a name recurring later in the text is left alone.)
static void ReplaceFirstIgnoreCase(std::string &str, const std::string &from, const std::string &to) {
	if (from.empty()) return;
	// Both sides are the game's own text -- a character's name, and their description -- so
	// neither is necessarily English, and the comparison is the frontend's to make.
	std::vector<size_t> origin;
	const std::string haystack = FoldRecordingOrigins(str, origin);
	const std::string needle = frontend->StrToLowerCase(from);
	const size_t at = haystack.find(needle);
	if (at == std::string::npos) return;
	// Back to where that was in the untouched string. The match's length there is its own: the
	// name as written may take a different number of bytes than the name folded.
	const size_t begin = origin[at];
	const size_t end = origin[at + needle.size()];
	str.replace(begin, end - begin, to);
}

// Stands in for a character's own name while grouping characters that share a
// here-description, so that the names can be substituted back as a single list.
static const char *const kCharNamePlaceholder = "##CHARNAME##";

bool Location::HoldsDirectly(const GameObj *obj) const {
	switch (obj->GetParentRelation()) {
	case HoldingType::AtLocation:
		return obj->GetParentKey() == key;
	case HoldingType::AtLocationGroup: {
		const auto *grp = Game::Get()->GetGroup(obj->GetParentKey());
		return grp && grp->ContainsObj(key);
	}
	case HoldingType::Everywhere:
		return true;
	default:
		return false;
	}
}

bool Location::IsCharVisibleHere(const Character *ch) const {
	// A character's visibility ceiling is the location they are ultimately standing in,
	// unless something opaque (a closed container) hides them along the way.
	const std::string &ceiling = ch->GetVisbilityCeiling();
	if (ceiling.empty())  // hidden characters are nowhere at all
		return false;
	if (Game::Get()->GroupExists(ceiling)) {
		const auto *grp = Game::Get()->GetGroup(ceiling);
		return grp && grp->ContainsObj(key);
	}
	return ceiling == key;
}

std::string Location::GetDescription(bool forDisplay) const {
	auto *theGame = Game::Get();

	// The ADRIFT Runner gives the player (not the author!) the ability to
	// disable showing the location name before the description. I don't feel
	// that feature is worth wiring an entire user settings mechanism into the
	// interpreter core, so I'm just not going to bother and always display them:
	std::string result = "<b>" + GetDisplayName() + "</b>\n";
	std::string ownDescription = GameObj::GetDescription(forDisplay);
	result += ownDescription;

	// Whether any descriptive content precedes the general object list: the location's own
	// description, or an explicitly-list-described object appended below. This drives the
	// "There is ... here." vs "Also here is ..." choice, mirroring clsLocation.ViewLocation --
	// whose running text starts as the long description, *not* the location name. So a general
	// list that is the first mention of anything at all reads "There is X here."; once any prose
	// has come before it, the same list is folded in as "Also here is X." (The location name
	// header we prepend above is not part of this and must not tip the scales.)
	bool anyPrecedingContent = !ownDescription.empty();

	// Visible objects are listed after the description proper, mirroring what
	// clsLocation.ViewLocation does in the original implementation: dynamic objects
	// are listed unless explicitly excluded, static objects only when explicitly
	// included. Objects with a list description get it appended verbatim; the
	// remaining listable objects are collected into a single "Also here is ..." /
	// "There is ... here." sentence.
	std::vector<std::string> generalListed;
	for (const auto &objKey: theGame->GetObjectLoadOrder()) {
		const auto *obj = theGame->GetObject(objKey);
		if (AsCharacter(obj) || AsLocation(obj))
			continue;
		if (!HoldsDirectly(obj))
			continue;
		if (obj->IsDynamic() ? obj->GetBoolProp("ExplicitlyExclude") : !obj->GetBoolProp("ExplicitlyList"))
			continue;
		const char *listProp = obj->IsDynamic() ? "ListDescriptionDynamic" : "ListDescription";
		std::string listDesc;
		if (obj->HasProp(listProp))
			listDesc = theGame->GetDescription(obj->GetIntProp(listProp))->Build(forDisplay);
		if (listDesc.empty()) {
			generalListed.push_back(obj->GetDisplayName());
		} else {
			PadForAppend(result);
			result += listDesc;
			anyPrecedingContent = true;
		}
	}

	if (!generalListed.empty()) {
		std::string list = JoinNames(generalListed);
		PadForAppend(result);
		result += anyPrecedingContent ? ("Also here is " + list + ".")
		                              : ("There is " + list + " here.");
	}

	// Any event currently running a SetLook subevent that applies here gets spliced in next,
	// mirroring where ViewLocation folds in Adventure.htblEvents' LookText: after the listed
	// objects, before character "is here" lines. Events contribute in load order, same as ADRIFT
	// walking htblEvents.Values; more than one applying at once is a corner ADRIFT itself leaves
	// as "whatever order the hashtable gives you", so load order is as good a tiebreak as any.
	for (const auto &evKey: theGame->GetEventLoadOrder()) {
		std::string lookText = theGame->GetEvent(evKey)->LookOverrideText();
		if (lookText.empty()) continue;
		PadForAppend(result);
		result += lookText;
	}

	// Characters visible here are listed after the objects. Each contributes its
	// CharHereDesc property if it has one, or a plain "<name> is here." otherwise; an
	// explicitly empty CharHereDesc means the character isn't announced at all.
	// Characters whose descriptions differ only by their own name are listed together,
	// so two bystanders become "Bob and Alice are here." rather than two sentences.
	const std::string &playerKey = theGame->GetPlayerChar()->Key();
	std::vector<std::pair<std::string, std::vector<std::string>>> charDescs;
	for (const auto &objKey: theGame->GetObjectLoadOrder()) {
		if (objKey == playerKey)  // you are never listed to yourself
			continue;
		const auto *ch = AsCharacter(theGame->GetObject(objKey));
		if (!ch || !IsCharVisibleHere(ch))
			continue;
		std::string name = ch->GetDisplayName(false);
		std::string hereDesc;
		if (ch->HasProp("CharHereDesc")) {
			// A CharHereDesc using %CharacterName% means this character, not the player -- same
			// referral-character convention Character::GetDescription uses for its own text.
			theGame->SetInternalReference("referral-character", ch->Key());
			hereDesc = theGame->GetDescription(ch->GetIntProp("CharHereDesc"))->Build(forDisplay);
			theGame->ClearInternalReference("referral-character");
		} else {
			hereDesc = name + " is here.";
		}
		if (hereDesc.empty())
			continue;
		std::string grouped = hereDesc;
		ReplaceFirstIgnoreCase(grouped, name, kCharNamePlaceholder);
		auto it = std::find_if(charDescs.begin(), charDescs.end(),
			[&](const auto &e) { return e.first == grouped; });
		if (it == charDescs.end())
			charDescs.emplace_back(grouped, std::vector<std::string>{ name });
		else if (std::find(it->second.begin(), it->second.end(), name) == it->second.end())
			it->second.push_back(name);
	}
	for (const auto &entry: charDescs) {
		std::string desc = entry.first;
		if (entry.second.size() > 1)  // several characters sharing a description need a plural verb
			ReplaceAll(desc, " is ", " are ");
		ReplaceAll(desc, kCharNamePlaceholder, JoinNames(entry.second));
		PadForAppend(result);
		result += desc;
	}

	// The available exits are listed last, when the game asks for it (<ShowExits>), mirroring
	// where clsLocation.ViewLocation appends its "Exits are ..." sentence.
	if (theGame->ShowExits()) {
		std::string exitsLine = GetExitsLine();
		if (!exitsLine.empty()) {
			PadForAppend(result);
			result += exitsLine;
		}
	}

	// Fallback in case there is nothing to show
	if (Util::StringIsNullOrWhitespace(result))
		return "There is nothing of interest here.";
	return result;
}

// ADRIFT's compass order (DirectionsEnum): N, E, S, W, U, D, In, Out, NE, SE, SW, NW. Everything
// that lists a location's exits walks them in this order, as the original does.
static const char *const kCompassOrder[] = {
	"North", "East", "South", "West", "Up", "Down", "In", "Out",
	"NorthEast", "SouthEast", "SouthWest", "NorthWest",
};

std::string Location::GetListOfExits() const {
	std::string result;
	size_t count = 0;
	// In compass order, matching ADRIFT's own `location.Exits` (Global.vb), which walks
	// DirectionsEnum from North to NorthWest. Iterating the exit table itself would leave the
	// order up to the hash map -- which is not merely arbitrary but unstable, since it changes
	// when the table is copied.
	for (const char *canonical : kCompassOrder) {
		auto e = exits->find(canonical);
		if (e == exits->end()) continue;
		// add to result if unrestricted
		if (e->second.restr == 0) {
			if (count++ > 0)
				result += '|';
			result += e->first;
			continue;
		}
		// otherwise check if restriction passes
		const auto *restr = Game::Get()->GetRestriction(e->second.restr);
		if (restr->PassRestrictionBlock().first) {
			if (count++ > 0)
				result += '|';
			result += e->first;
		}
	}
	return result;
}

std::string Location::GetExitsLine() const {
	auto *theGame = Game::Get();
	const auto &dirTable = theGame->GetDirectionTable();
	std::vector<std::string> available;
	for (const char *canonical : kCompassOrder) {
		auto it = exits->find(canonical);
		if (it == exits->end())
			continue;
		// An exit with restrictions is only listed if they currently pass, mirroring
		// clsCharacter.HasRouteInDirection (which evaluates each direction's restrictions).
		if (it->second.restr != 0 && !theGame->GetRestriction(it->second.restr)->PassRestrictionBlock().first)
			continue;
		auto disp = dirTable.canonicalToDisplay.find(canonical);
		available.push_back(frontend->StrToLowerCase(
			disp != dirTable.canonicalToDisplay.end() ? disp->second : std::string(canonical)));
	}
	if (available.empty())
		return "";
	if (available.size() == 1)
		return "An exit leads " + available.front() + ".";
	return "Exits are " + JoinNames(available) + ".";
}

bool Location::IsAdjacent(const std::string &locKey) const {
	for (const auto &e : *exits)
		if (e.second.destination == locKey)
			return true;
	return false;
}

std::string Location::DirectionTo(const std::string &locKey) const {
	if (locKey == key) return "not moved";
	// ADRIFT's compass order, so that a location reachable two ways is reported by the first of
	// them. Paired with the phrasing ADRIFT slots into "<name> exits to <...>" / "enters from <...>".
	static const std::pair<const char *, const char *> kDirs[] = {
		{"North", "the north"}, {"East", "the east"}, {"South", "the south"}, {"West", "the west"},
		{"Up", "above"}, {"Down", "below"}, {"In", "inside"}, {"Out", "outside"},
		{"NorthEast", "the north-east"}, {"SouthEast", "the south-east"},
		{"SouthWest", "the south-west"}, {"NorthWest", "the north-west"},
	};
	for (const auto &d : kDirs) {
		auto it = exits->find(d.first);
		if (it != exits->end() && it->second.destination == locKey)
			return d.second;
	}
	return "nowhere";
}

void Location::MakeMatchExpr() {
	// This expression requires a string to both begin and not begin with the letter x,
	// thus it can never match.
	matchRegex = std::regex("^(?!x)x");
}

}