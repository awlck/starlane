//
// Created by Adrian Welcker on 11.07.23.
//

#include "game.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include "gamecontent/description.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/synonym.h"
#include "gamecontent/character.h"
#include "gamecontent/utility.h"

namespace Starlane {

namespace {
enum class ReferenceType {
	Object,
	Character
};

// A reference name captured out of a Command pattern (e.g. "%direction%", "%object1%",
// "%text%") carries a numeric suffix (1-5) for disambiguating multiple references of the
// same kind within one command. This splits off the surrounding '%' and that suffix, giving
// the "family" the reference belongs to (e.g. "direction", "object", "text") and the suffix
// itself (e.g. "1", or "" for an unnumbered reference like plain "%object%").
struct RefNameParts {
	std::string family;
	std::string suffix;
};

RefNameParts SplitRefName(const std::string &refName) {
	RefNameParts result;
	result.family = refName.substr(1, refName.size() - 2);
	if (!result.family.empty() && std::isdigit((unsigned char) result.family.back())) {
		result.suffix = result.family.substr(result.family.size() - 1);
		result.family.pop_back();
	}
	result.family = Util::ToLower(result.family);
	return result;
}

// Besides the literal name used in a task's own Command pattern (e.g. "%object1%"), library
// restrictions shared across many tasks address references generically, by position, using
// ADRIFT's built-in "ReferencedObject"/"ReferencedObject1".."5"/etc. names (see Util::IsReference).
// Returns "" for families that have no such generic alias (e.g. free-form text).
std::string GenericAliasFamily(const std::string &family) {
	if (family == "object") return "ReferencedObject";
	if (family == "objects") return "ReferencedObjects";
	if (family == "character") return "ReferencedCharacter";
	if (family == "direction") return "ReferencedDirection";
	if (family == "location") return "ReferencedLocation";
	if (family == "item") return "ReferencedItem";
	if (family == "number") return "ReferencedNumber";
	if (family == "text") return "ReferencedText";
	return "";
}

// The inverse of Util::SplitList: combine several object/character keys into the '|'-joined form
// a reference bound to more than one of them at once is represented as (see FlushResponseBuffer's
// merging of aggregated runs).
std::string JoinKeys(const std::vector<std::string> &keys) {
	std::string joined;
	for (size_t i = 0; i < keys.size(); i++) {
		if (i) joined += '|';
		joined += keys[i];
	}
	return joined;
}

// A system command is matched against the player's input directly rather than through a task's
// Command pattern, so it has to make its own arrangements for the leniency those (case-insensitive)
// regexes afford: fold case, and ignore surrounding whitespace.
std::string NormalizeSystemCommand(const std::string &s) {
	static const char *kBlank = " \t\r\n";  // a frontend reading CRLF input leaves the '\r' on
	size_t first = s.find_first_not_of(kBlank);
	if (first == std::string::npos) return "";
	std::string result = s.substr(first, s.find_last_not_of(kBlank) - first + 1);
	// Folded by the frontend rather than here: this is the player's own typing, which is not
	// necessarily English, and case is a question only something that knows about languages can
	// answer (Qt hands it to QString; a Glk frontend will hand it to its host).
	return frontend->StrToLowerCase(result);
}

// Whether two pieces of text are the same but for case. Both sides go through the frontend for
// the same reason as above -- one of them is what the player typed, the other is the game's own
// text -- so no assumption is made about either being ASCII. Notably that rules out comparing
// lengths first: folding can change a string's length ("SS" folds to a single "ß" and back).
bool CaseInsensitiveEq(const std::string &a, const std::string &b) {
	return frontend->StrToLowerCase(a) == frontend->StrToLowerCase(b);
}

// Whether a %ref% family names a physical thing that "it"/"them" could stand for -- as opposed
// to a character (which gets "him"/"her"/"it" depending on Gender instead) or a family with no
// sensible antecedent at all (direction, number, free text, ...).
bool IsObjectPronounFamily(const std::string &family) {
	return family == "object" || family == "objects" || family == "item";
}
bool IsCharacterPronounFamily(const std::string &family) {
	return family == "character" || family == "characters";
}

// The word a bare verb's missing reference gets asked about with ("Launch what?"/"who?"/
// "where?"), for whichever of the three families PromptForIncompleteVerb recognizes -- or
// nullptr for a family it doesn't (number, text, location: ADRIFT's NotUnderstood doesn't
// prompt for those either).
const char *MissingRefPromptWord(const std::string &family) {
	if (IsObjectPronounFamily(family)) return "what";
	if (IsCharacterPronounFamily(family)) return "who";
	if (family == "direction") return "where";
	return nullptr;
}

}  // anonymous namespace

std::string Game::SubstitutePronouns(std::string s) const {
	static const std::regex kItWord(R"(\bit\b)");
	static const std::regex kThemWord(R"(\bthem\b)");
	static const std::regex kHimWord(R"(\bhim\b)");
	static const std::regex kHerWord(R"(\bher\b)");
	// A pronoun the player used is answered with what it was taken to mean, on a line of its own
	// ahead of the command's own output -- "(the thin book)" before READ IT does its reading.
	// ADRIFT prints this as it makes each substitution, so several pronouns in one command produce
	// several lines, in this same order.
	auto substitute = [this, &s](const std::regex &wordRe, const std::string &antecedent) {
		if (antecedent.empty() || !std::regex_search(s, wordRe)) return;
		OutputFiltered("<c>(" + antecedent + ")</c>\n");
		s = std::regex_replace(s, wordRe, antecedent);
	};
	// Order matches ADRIFT: an antecedent's own display name never itself contains one of these
	// four words as a whole word, so one pass per pronoun (rather than a single combined regex)
	// is enough, and keeps each replacement independent of the others.
	substitute(kItWord, pronounItText);
	substitute(kThemWord, pronounThemText);
	substitute(kHimWord, pronounHimText);
	substitute(kHerWord, pronounHerText);
	return s;
}

void Game::UpdatePronounAntecedents() {
	auto joinDefiniteNames = [this](const std::vector<std::string> &keys) {
		std::string result;
		for (size_t i = 0; i < keys.size(); i++) {
			if (i != 0) result += (i + 1 == keys.size()) ? " and " : ", ";
			const GameObj *ob = TryGetObject(keys[i]);
			result += ob ? ob->GetDisplayName(true) : keys[i];
		}
		return result;
	};

	for (const auto &token : currentMatchedRefTokens) {
		const std::string &family = SplitRefName(token).family;
		bool isObjFamily = IsObjectPronounFamily(family);
		bool isCharFamily = IsCharacterPronounFamily(family);
		if (!isObjFamily && !isCharFamily) continue;

		// A plural reference that named several things at once ("take the plates and the ration
		// bar") sets "them" to the whole group.
		auto listIt = std::find_if(currentRefLists.begin(), currentRefLists.end(),
		                            [&](const auto &p) { return p.first == token; });
		if (listIt != currentRefLists.end()) {
			pronounThemText = joinDefiniteNames(listIt->second);
			continue;
		}

		auto refIt = currentRefs.find(Util::CanonicalizeRefName(token));
		if (refIt == currentRefs.end() || refIt->second.empty()) continue;
		const GameObj *ob = TryGetObject(refIt->second);
		if (!ob) continue;

		if (isCharFamily && ob->HasProp("Gender")) {
			const std::string gender = ob->GetStrProp("Gender");
			if (gender == "Male") { pronounHimText = ob->GetDisplayName(true); continue; }
			if (gender == "Female") { pronounHerText = ob->GetDisplayName(true); continue; }
		}
		pronounItText = ob->GetDisplayName(true);
	}
}

std::string Game::ApplySynonyms(std::string s) {
	for (const auto &[fst, snd]: staticData->synonyms) {
		for (const auto &f: snd->GetFrom()) {
			size_t n;
			while ((n = s.find(f)) != std::string::npos)
				s.replace(n, f.size(), snd->GetReplacement());
		}
	}
	return s;
}

std::vector<std::string> Game::MatchListForReference(const std::string &from, const std::string &refFamily,
                                                     bool plural) const {
	using namespace std::string_literals;
	ReferenceType rt;
	if (refFamily.substr(0, sizeof("object")-1) == "object"s) rt = ReferenceType::Object;
	else if (refFamily.substr(0, sizeof("character")-1) == "character"s) rt = ReferenceType::Character;
	else throw std::runtime_error("Unknown reference type in task: " + refFamily);

	// Iterate in load order (not the objects hash map's arbitrary order) so that both the
	// provisional pick and any disambiguation prompt list candidates as the game defines them --
	// "the red ball or the green ball", the order the author wrote them, as ADRIFT does.
	std::vector<std::string> result;
	for (const GameObj *o : objects) {
		switch (rt) {
			case ReferenceType::Object:
				if (o->IsCharacter()) continue;
				break;
			case ReferenceType::Character:
				if (!o->IsCharacter()) continue;
				break;
		}
		if (std::regex_match(from, plural ? o->GetPluralMatchExpr() : o->GetMatchExpr()))
			result.push_back(o->Key());
	}

	// Narrow the name matches down by scope, preferring the narrowest scope that still
	// leaves us with at least one object: first things the player can currently see,
	// then things they have seen at some point. If neither yields anything, keep the
	// full list; the matched task's own restrictions will then produce a sensible
	// failure message (e.g. "You see no such thing.").
	const auto *player = AsCharacter(GetPlayerChar());
	if (!player) return result;
	std::vector<std::string> visible, seen;
	for (const auto &k : result) {
		if (player->CanSee(k))
			visible.push_back(k);
		else if (player->HasSeen(k))
			seen.push_back(k);
	}
	if (!visible.empty()) return visible;
	if (!seen.empty()) return seen;
	return result;
}

// Every name one reference answers to. A task's own Command pattern spells it one way
// ("%object1%"), which is what that task's own restriction and message text will say; the library
// restrictions shared across many tasks instead use ADRIFT's generic, position-based name
// ("ReferencedObject1"). And "%object%" and "%object1%" are the same, first reference, which a task
// is free to mix between its Command and its messages -- the library's "open objects" task does
// exactly that. Anything recording something *about* a reference has to record it under all of
// them, or a lookup that arrives by another spelling comes up empty.
std::vector<std::string> Game::ReferenceAliases(const std::string &ref) {
	auto [family, suffix] = SplitRefName(ref);
	std::vector<std::string> names{ Util::CanonicalizeRefName(ref) };
	const std::string alias = GenericAliasFamily(family);
	if (!alias.empty())
		names.push_back(Util::CanonicalizeRefName(alias + suffix));
	if (suffix.empty() || suffix == "1") {
		const std::string other = suffix.empty() ? "1" : "";
		names.push_back(Util::CanonicalizeRefName('%' + family + other + '%'));
		if (!alias.empty())
			names.push_back(Util::CanonicalizeRefName(alias + other));
	}
	return names;
}

void Game::BindReference(std::unordered_map<std::string, std::string> &refs,
                         const std::string &ref, const std::string &value) {
	for (const auto &name : ReferenceAliases(ref))
		refs[name] = value;
}

namespace {
// Whether the text filling a plural reference is ADRIFT's ALL keyword rather than the name of
// anything. "all" on its own only: "all balls" and "all but the ball" narrow it, which is a
// different (and unimplemented) matter, and must not be mistaken for the sweeping form.
bool IsAllKeyword(const std::string &raw) {
	const std::string folded = Util::ToLower(raw);
	size_t first = folded.find_first_not_of(" \t");
	if (first == std::string::npos) return false;
	const std::string word = folded.substr(first, folded.find_last_not_of(" \t") - first + 1);
	return word == "all" || word == "everything";
}
}  // anonymous namespace

bool Game::CaptureReferences(const std::vector<std::string> &refSpecs, const std::smatch &matches) {
	currentRefLists.clear();
	currentRefItemMatches.clear();
	currentAllRefs.clear();
	for (size_t i = 0; i < refSpecs.size(); i++) {
		const std::string &ref = refSpecs[i];
		std::string raw = matches[i + 1].str();
		auto [family, suffix] = SplitRefName(ref);

		std::string resolved;
		if (family == "text" || family == "number") {
			// Kept verbatim: text is opaque, and numbers are parsed on demand by expressions.
			resolved = raw;
		} else if (family == "direction") {
			// May resolve to "" if somehow not a recognized direction word; that's fine,
			// restrictions/messages relying on it will simply see an empty reference.
			resolved = Util::CanonicalizeDirection(raw, GetDirectionTable());
		} else if (family == "objects" || family == "characters") {
			// ALL names everything the player has laid eyes on, rather than anything in particular.
			// ADRIFT expands it against htblObjects.SeenBy and leaves the sorting-out to the task's
			// restrictions, which run once per thing (see ExecuteMatchedTask) and quietly reject
			// whatever the command cannot apply to.
			if (IsAllKeyword(raw)) {
				const auto *player = AsCharacter(GetPlayerChar());
				std::vector<std::string> items;
				const bool wantChars = family == "characters";
				for (const GameObj *o : objects) {
					if (o->IsLocation() || o->IsCharacter() != wantChars) continue;
					if (o->Key() == playerKey) continue;  // "take all" is never about yourself
					if (player && !player->HasSeen(o->Key())) continue;
					items.push_back(o->Key());
				}
				if (items.empty()) return false;
				// Under every spelling: the standard library keeps a task out of a sweeping command
				// with "ReferencedObjects MustNot BeExactText All", by the generic name, while the
				// task's own Command called it "%objects%".
				for (const auto &name : ReferenceAliases(ref))
					currentAllRefs.insert(name);
				resolved = items.front();
				// Recorded as a plural reference even when only one thing survives, so that the
				// odometer in ExecuteMatchedTask drives it and each thing is judged on its own.
				std::vector<RefMatchInfo> itemMatches;
				itemMatches.reserve(items.size());
				for (const auto &k : items)
					itemMatches.push_back({raw, {k}});
				currentRefItemMatches[Util::CanonicalizeRefName(ref)] = std::move(itemMatches);
				currentRefLists.emplace_back(ref, std::move(items));
				BindReference(currentRefs, ref, resolved);
				continue;
			}
			// Before reading the text as a list of things, read it as one *plural* thing: "get
			// tubs" names both objects called a "tub" at once. ADRIFT tries this first too
			// (InputMatchesObjects' "objects1" case recurses with bPlural set, and only falls
			// through to the comma/"and" form when nothing answers to the plural). Everything the
			// plural named becomes its own item, already settled -- naming things by the kind they
			// are is not ambiguous the way naming one of them by a shared noun is, so there is
			// nothing to ask about. Objects only: ADRIFT gives characters no plural forms.
			if (family == "objects") {
				auto pluralMatches = MatchListForReference(raw, family, /*plural =*/ true);
				if (!pluralMatches.empty()) {
					std::vector<RefMatchInfo> itemMatches;
					itemMatches.reserve(pluralMatches.size());
					for (const auto &k : pluralMatches)
						itemMatches.push_back({raw, {k}});
					currentRefItemMatches[Util::CanonicalizeRefName(ref)] = std::move(itemMatches);
					BindReference(currentRefs, ref, pluralMatches.front());
					currentRefLists.emplace_back(ref, std::move(pluralMatches));
					continue;
				}
			}
			// A plural reference can name several things at once ("take the plates and the ration
			// bar"). Each one is resolved separately, and the task runs once per thing; see
			// ExecuteMatchedTask. The reference itself starts out bound to the first of them.
			const auto pieces = Util::SplitObjectList(raw);
			if (pieces.empty()) return false;
			std::vector<std::string> items;
			std::vector<RefMatchInfo> itemMatches;
			for (const auto &piece : pieces) {
				auto matchList = MatchListForReference(piece, family);
				if (matchList.empty()) return false;
				if (pieces.size() == 1) {
					// The ordinary case: one thing named, which may still be named ambiguously.
					// Record it exactly as a singular reference does, so that "take ball" with two
					// balls present asks rather than silently picking one -- "take" is spelled
					// %objects% in the standard library, so this is the path it goes down.
					currentRefMatches[Util::CanonicalizeRefName(ref)] = {raw, matchList};
				}
				items.push_back(matchList.front());
				itemMatches.push_back({piece, std::move(matchList)});
			}
			resolved = items.front();
			// When the player names several things, each is provisionally its first match; any piece
			// that is itself ambiguous ("... and the ball" with two balls) is remembered per-item so
			// BeginDisambiguationIfNeeded can ask about it, mirroring the singular case above.
			if (items.size() > 1) {
				currentRefLists.emplace_back(ref, std::move(items));
				currentRefItemMatches[Util::CanonicalizeRefName(ref)] = std::move(itemMatches);
			}
		} else {
			// Objects, characters, locations, items: resolve the raw text to an actual game object.
			auto matchList = MatchListForReference(raw, family);
			if (matchList.empty()) return false;
			// Take the first match as the provisional resolution so restrictions can be checked,
			// but keep the whole list (and the raw text the player typed): should this reference
			// belong to the task we end up running and have matched more than one thing,
			// BeginDisambiguationIfNeeded will ask the player which they meant.
			resolved = matchList.front();
			currentRefMatches[Util::CanonicalizeRefName(ref)] = {raw, std::move(matchList)};
		}

		BindReference(currentRefs, ref, resolved);
	}
	return true;
}

bool Game::RefineReferencesByRestrictions(const Task *task,
                                          const std::vector<std::string> &refTokens) {
	// ADRIFT narrows an ambiguous reference against the task's own restrictions before it ever
	// considers asking the player (RefineMatchingPossibilitesUsingRestrictions). That is what makes
	// GET PIN take the map pin without comment when the only other pin is already in your hand: the
	// standard TAKE task will not take something you are holding, so that candidate drops out.
	// Each reference is narrowed with the others held at their current binding, rather than over the
	// full cross product ADRIFT walks -- the difference only shows when two references of one command
	// are ambiguous at once and their restrictions are entangled.
	bool anyWasAmbiguous = false;
	bool anyEmptied = false;

	auto refine = [&](RefMatchInfo &info, const std::string &token, std::vector<std::string> *listSlot,
	                  size_t itemIdx) {
		if (info.candidates.size() < 2) return;
		anyWasAmbiguous = true;
		const std::string original = currentRefs[Util::CanonicalizeRefName(token)];
		std::vector<std::string> kept;
		for (const std::string &candidate : info.candidates) {
			BindReference(currentRefs, token, candidate);
			if (listSlot && itemIdx < listSlot->size()) (*listSlot)[itemIdx] = candidate;
			if (task->Eligible().first) kept.push_back(candidate);
		}
		if (kept.empty()) {
			// Nothing the player could have meant works here. Leave the candidates alone and let the
			// caller pass this task over: asking "which pin?" about things that would all be refused
			// anyway is exactly the noise ADRIFT avoids by skipping to the next task.
			anyEmptied = true;
			BindReference(currentRefs, token, original);
			if (listSlot && itemIdx < listSlot->size()) (*listSlot)[itemIdx] = original;
			return;
		}
		info.candidates = std::move(kept);
		BindReference(currentRefs, token, info.candidates.front());
		if (listSlot && itemIdx < listSlot->size()) (*listSlot)[itemIdx] = info.candidates.front();
	};

	for (const std::string &token : refTokens) {
		const std::string canonical = Util::CanonicalizeRefName(token);
		auto singular = currentRefMatches.find(canonical);
		if (singular != currentRefMatches.end())
			refine(singular->second, token, nullptr, 0);

		auto items = currentRefItemMatches.find(canonical);
		if (items == currentRefItemMatches.end()) continue;
		auto listIt = std::find_if(currentRefLists.begin(), currentRefLists.end(),
		                           [&](const auto &p) { return p.first == token; });
		std::vector<std::string> *listSlot = listIt == currentRefLists.end() ? nullptr : &listIt->second;
		for (size_t i = 0; i < items->second.size(); i++)
			refine(items->second[i], token, listSlot, i);

		// An item of a plural reference that the task would refuse is dropped from the list rather
		// than answered, so long as something else in the list survives: ADRIFT's refinement pass
		// only ever collects the combinations that pass, and an item none of whose candidates passed
		// is simply never collected. That is why "put all in bag" reports what went in and not the
		// hundred things that didn't -- and equally why "drop bowl and mortar", with the mortar
		// nowhere in reach, answers about the bowl alone.
		if (!listSlot) continue;
		std::vector<RefMatchInfo> keptItems;
		std::vector<std::string> keptKeys;
		for (size_t i = 0; i < items->second.size(); i++) {
			BindReference(currentRefs, token, (*listSlot)[i]);
			if (!task->Eligible().first) continue;
			keptItems.push_back(items->second[i]);
			keptKeys.push_back((*listSlot)[i]);
		}
		if (keptKeys.empty()) {
			// Nothing named can be used at all. ADRIFT puts the whole reference back as it was
			// (bResetRef), leaving the task to refuse it by name -- so an explicitly named thing
			// still gets told why. ALL is the exception: there is no name to answer about, and the
			// sweep is meant to pass over to whatever handles a wholly fruitless one.
			if (currentAllRefs.count(canonical))
				anyEmptied = anyWasAmbiguous = true;
			continue;
		}
		items->second = std::move(keptItems);
		*listSlot = std::move(keptKeys);
		BindReference(currentRefs, token, listSlot->front());
	}
	// Only a reference that was ambiguous to begin with can veto the task this way; one the player
	// named exactly still runs, so that the task's own restriction gets to explain the refusal.
	return !(anyWasAmbiguous && anyEmptied);
}

Task *Game::FindMatchingTask() {
	// Only used as a fallback under ExecutionPolicy::HighestPrioPassing: the first
	// failing-with-message match encountered, in case no task ever passes restrictions.
	Task *fallback = nullptr;
	std::vector<std::string> fallbackRefTokens;
	std::unordered_map<std::string, std::string> fallbackRefs;
	std::unordered_map<std::string, RefMatchInfo> fallbackRefMatches;
	std::vector<std::pair<std::string, std::vector<std::string>>> fallbackRefLists;
	std::unordered_map<std::string, std::vector<RefMatchInfo>> fallbackRefItemMatches;
	// The highest-priority task whose command matched but whose %ref%s named nothing the game
	// knows. If nothing better turns up, ADRIFT runs this one anyway (its sNoRefTask) so that its
	// own "must exist" restriction can say something useful -- "Launch what?" beats "I didn't
	// understand that sentence."
	Task *noRefTask = nullptr;
	std::vector<std::string> noRefTokens;
	// The highest-priority task whose command matched but that could not tell which of several
	// objects the player meant. ADRIFT does not stop there: it notes the task (sAmbTask) and keeps
	// looking, and only asks "Which pin?" if nothing further down the list can run either -- so a
	// lower-priority task whose own restrictions single one of them out gets the command instead.
	Task *ambTask = nullptr;
	std::vector<std::string> ambRefTokens;
	std::unordered_map<std::string, std::string> ambRefs;
	std::unordered_map<std::string, RefMatchInfo> ambRefMatches;
	std::vector<std::pair<std::string, std::vector<std::string>>> ambRefLists;
	std::unordered_map<std::string, std::vector<RefMatchInfo>> ambRefItemMatches;

	// Folded once here for Task::GetCmdLiterals below, which holds its literals folded the same
	// way. The command usually arrives folded already, but not always: SubstitutePronouns splices
	// in a thing's display name ("take it" -> "take the Brass Lantern") as the game spells it.
	const std::string foldedCommand = Util::ToLower(currentCommand);

	for (Task *task : staticData->prioOrderedTasks) {
		if (task->GetType() != Task::Type::General) continue;
		// A completed, non-repeatable task is not a candidate at all.
		if (task->Completed() && !task->IsRepeatable()) continue;

		const auto &regexes = task->GetCmdRegexes();
		const auto &literals = task->GetCmdLiterals();
		const auto &groupCoding = task->GetGroupCoding();
		for (size_t cmdIdx = 0; cmdIdx < regexes.size(); cmdIdx++) {
			// Cheap first: a pattern whose mandatory literal text isn't in the input cannot match,
			// and a game has thousands of patterns for every command the player types.
			const std::string &required = literals[cmdIdx];
			if (!required.empty() && foldedCommand.find(required) == std::string::npos) continue;
			std::smatch matches;
			if (!std::regex_match(currentCommand, matches, regexes[cmdIdx])) continue;

			// References must be captured *before* checking eligibility: restrictions
			// (e.g. "must have a route in %direction%") need to see the values the player
			// actually typed.
			currentRefs.clear();
			currentRefMatches.clear();
			if (!CaptureReferences(groupCoding[cmdIdx], matches)) {
				if (!noRefTask) {
					noRefTask = task;
					noRefTokens = groupCoding[cmdIdx];
				}
				continue;
			}

			// Narrow any reference that matched several things down to those the task would
			// actually accept, before its restrictions are consulted for real below.
			if (!RefineReferencesByRestrictions(task, groupCoding[cmdIdx])) {
				if (!noRefTask) {
					noRefTask = task;
					noRefTokens = groupCoding[cmdIdx];
				}
				continue;
			}

			// Still more than one candidate for some reference after that narrowing: remember the
			// task in case nothing else can run, and move on.
			std::string ambToken;
			int ambItemIdx;
			if (FirstAmbiguousSlot(groupCoding[cmdIdx], currentRefMatches, currentRefItemMatches,
			                       ambToken, ambItemIdx) != nullptr) {
				if (!ambTask) {
					ambTask = task;
					ambRefTokens = groupCoding[cmdIdx];
					ambRefs = currentRefs;
					ambRefMatches = currentRefMatches;
					ambRefLists = currentRefLists;
					ambRefItemMatches = currentRefItemMatches;
				}
				continue;
			}

			auto result = task->Eligible();
			// A task that fails restrictions with no message at all isn't a real candidate
			// under either policy -- it has nothing to say for itself, so a lower-priority
			// (higher-numbered) task's command pattern still deserves a shot at matching too.
			if (!result.first && result.second == 0) continue;

			if (result.first || staticData->executionPolicy == ExecutionPolicy::HighestPrio) {
				// Either this task passes outright, or (under HighestPrio) it's simply the
				// first real candidate at all: stop looking, whether it passes or fails.
				currentMatchedRefTokens = groupCoding[cmdIdx];
				return task;
			}
			// HighestPrioPassing: this candidate fails (but has something to say); keep
			// scanning for one that passes, remembering the first failing one as a fallback.
			if (!fallback) {
				fallback = task;
				fallbackRefTokens = groupCoding[cmdIdx];
				fallbackRefs = currentRefs;
				fallbackRefMatches = currentRefMatches;
				fallbackRefLists = currentRefLists;
				fallbackRefItemMatches = currentRefItemMatches;
			}
		}
	}

	if (fallback) {
		currentRefs = std::move(fallbackRefs);
		currentMatchedRefTokens = std::move(fallbackRefTokens);
		currentRefMatches = std::move(fallbackRefMatches);
		currentRefLists = std::move(fallbackRefLists);
		currentRefItemMatches = std::move(fallbackRefItemMatches);
		return fallback;
	}
	if (ambTask) {
		// Nothing else could run, so the question really does have to be asked.
		currentRefs = std::move(ambRefs);
		currentMatchedRefTokens = std::move(ambRefTokens);
		currentRefMatches = std::move(ambRefMatches);
		currentRefLists = std::move(ambRefLists);
		currentRefItemMatches = std::move(ambRefItemMatches);
		return ambTask;
	}
	if (noRefTask) {
		// Nothing was resolved, so nothing is ambiguous either: run the task on empty references
		// and let its own restrictions do the talking.
		currentRefs.clear();
		currentRefMatches.clear();
		currentRefLists.clear();
		currentRefItemMatches.clear();
		currentMatchedRefTokens = std::move(noRefTokens);
	}
	return noRefTask;
}

bool Game::PromptForIncompleteVerb() {
	// Only for a single bare word: "launch" with nothing after it. Multi-word input that still
	// matched nothing falls to DescribeUnmatchedThing instead.
	if (currentCommand.empty() || currentCommand.find(' ') != std::string::npos) return false;

	// Deliberately not filtered by Completed()/IsRepeatable() here, unlike FindMatchingTask: a
	// command whose only accepting task is used up still shaped the player's input, and ADRIFT's
	// own NotUnderstood scans every task's command text regardless of completion for this check.
	for (Task *task : staticData->prioOrderedTasks) {
		if (task->GetType() != Task::Type::General) continue;
		const auto &regexes = task->GetCmdRegexes();
		const auto &groupCoding = task->GetGroupCoding();
		for (size_t cmdIdx = 0; cmdIdx < regexes.size(); cmdIdx++) {
			for (const auto &ref : groupCoding[cmdIdx]) {
				const std::string family = SplitRefName(ref).family;
				const char *word = MissingRefPromptWord(family);
				if (!word) continue;
				// A dummy value, appended to the bare verb, tests whether this command's shape
				// accepts the verb plus *something* here -- without needing to actually resolve
				// what that something refers to. An object/character reference's regex fragment
				// accepts any text, so a nonsense word does the job; %direction%'s fragment only
				// accepts real direction words, so it gets a real one instead. Mirrors ADRIFT's
				// own NotUnderstood, which probes the same distinction the same way.
				const std::string probe = currentCommand + " " +
					(family == "direction" ? "north" : "zzyzx-nonsense-zzyzx");
				if (!std::regex_match(probe, regexes[cmdIdx])) continue;
				// Remembered, so that a bare answer to the question we are about to ask ("the
				// ship") is read as the whole command ("launch the ship") -- see rememberedVerb.
				rememberedVerb = currentCommand;
				OutputFiltered(frontend->StrToSentenceCase(currentCommand) + " " + word + "?\n");
				return true;
			}
		}
	}
	return false;
}

bool Game::DescribeUnmatchedThing() {
	const auto *player = AsCharacter(GetPlayerChar());
	if (!player) return false;
	// Searched, not matched: the name has only to appear *somewhere* in what the player typed, so
	// that "shoot larger alien" -- a verb this game has no task for -- is still answered about the
	// alien rather than rejected out of hand. ADRIFT scans with an unanchored Regex.IsMatch here
	// for exactly that reason (it matches the whole input elsewhere, when it means to).
	auto named = [&](const GameObj *o) {
		return player->HasSeen(o->Key()) && player->CanSee(o->Key())
			&& std::regex_search(currentCommand, o->GetMatchExpr());
	};
	// Objects before characters, as ADRIFT checks them, and each named the way it names them: an
	// object by its definite article ("the rock"), a character by the indefinite form its own
	// naming produces ("a larger alien") -- or by their proper name once known, which
	// Character::GetDisplayName already accounts for. Load order within each, as everywhere else
	// things are listed for the player.
	for (const GameObj *o : objects)
		if (!o->IsCharacter() && !o->IsLocation() && named(o)) {
			OutputFiltered("I don't understand what you want to do with " + o->GetDisplayName(true) + ".\n");
			return true;
		}
	for (const GameObj *o : objects)
		if (o->IsCharacter() && named(o)) {
			OutputFiltered("I don't understand what you want to do with " + o->GetDisplayName(false) + ".\n");
			return true;
		}
	return false;
}

const std::vector<Task *> &Game::GetSpecificChildren(const std::string &generalKey) const {
	static const std::vector<Task *> kEmpty;
	auto it = staticData->specificChildren.find(generalKey);
	return it == staticData->specificChildren.end() ? kEmpty : it->second;
}

bool Game::TaskIsSpecificChildOf(const std::string &childKey, const std::string &parentKey) const {
	for (const Task *child : GetSpecificChildren(parentKey))
		if (child->Key() == childKey)
			return true;
	return false;
}

bool Game::SpecificTaskMatches(const Task *specific, const std::vector<std::string> &refTokens) const {
	const auto &specifics = specific->GetSpecificRefs();
	// A Specific task's %ref% constraints are positional, mirroring the general command's own
	// %ref% tokens; if the counts don't match, this Specific task doesn't apply to the
	// particular command pattern that was matched (e.g. it targets a differently-shaped
	// alternate Command line on the same General task).
	if (specifics.size() != refTokens.size()) return false;

	for (size_t i = 0; i < specifics.size(); i++) {
		const auto &spec = specifics[i];
		if (spec.keys.empty()) continue;  // wildcard: matches any value for this reference

		if (spec.keys.size() > 1) {
			// This reference must have named exactly this set of objects together, e.g. "put the
			// fob key and the tube in the box" (cf. Race Against Time's cl_PutABlueFo). The full
			// set of things a plural reference named lives in currentRefLists, independent of
			// whichever single one of them ExecuteMatchedTask's per-object odometer loop currently
			// has currentRefs bound to -- so this checks the named set directly rather than the
			// current single-object binding, and fires on every odometer iteration that named set
			// produced (each suppressing its own object's share of the parent's behavior).
			auto listIt = std::find_if(currentRefLists.begin(), currentRefLists.end(),
				[&](const auto &p) { return p.first == refTokens[i]; });
			if (listIt == currentRefLists.end()) return false;
			const auto &items = listIt->second;
			if (items.size() != spec.keys.size()) return false;
			for (const auto &k : spec.keys) {
				if (std::find(items.begin(), items.end(), k) == items.end()) return false;
			}
			continue;
		}

		auto it = currentRefs.find(Util::CanonicalizeRefName(refTokens[i]));
		if (it == currentRefs.end()) return false;

		const std::string &key = spec.keys.front();
		if (spec.type == Task::SpType::Text) {
			if (!CaseInsensitiveEq(it->second, key)) return false;
		} else {
			const std::string &want = (key == "%Player%" || key == "Player") ? playerKey : key;
			if (it->second != want) return false;
		}
	}
	return true;
}

bool Game::RunTaskAndCapture(Task *task, bool showText, bool runActions) {
	bool msgFirst = task->GetMessagePlacement() == Task::MessagePlacement::Before;
	bool anyText = false;
	// `pinned`, when non-empty, is the message exactly as it read before this task's actions ran:
	// it is emitted verbatim instead of being rendered again at flush time. `at` is where in the
	// buffer it belongs -- the end, except for a Before message, which was read ahead of the actions
	// and so belongs ahead of whatever they recorded. See the Before-placement path below.
	auto emit = [&](const std::string &pinned, size_t at = SIZE_MAX) {
		if (!showText || task->GetCompletionMsg() == 0) return;
		const Description *desc = GetDescription(task->GetCompletionMsg());

		if (!activeResponseBuffer) {
			// Out-of-command path (event, walk, triggered task): unchanged from before -- build and
			// commit the message now (so a sequential/return-to-default description advances its
			// shown-state as it always did), dedup on the evaluated text turn-wide, emit immediately.
			std::string text = pinned.empty()
				? MutableDescription(task->GetCompletionMsg())->BuildAndCommit() : pinned;
			if (!text.empty()) anyText = true;
			if (completionMessagesThisTurn.insert(text).second) {
				responsesRecorded++;
				OutputFiltered(std::move(text));
			}
			return;
		}

		// Record a message that is already finished with: its dedup key is the text itself, so two
		// runs that read differently ("(from the wooden table)" / "(from the metal hook)") stay two
		// responses, each keeping its own place in the buffer. This is ADRIFT's behaviour --
		// AddResponse keys on the already-replaced string in exactly this case.
		auto recordFinished = [&](std::string text) {
			anyText = true;
			ResponseBuffer &buf = *activeResponseBuffer;
			if (buf.byKey.find(text) != buf.byKey.end()) return;
			AggregatedResponse resp;
			resp.descr = task->GetCompletionMsg();
			resp.pinnedText = text;
			resp.refSnapshot = currentRefs;
			buf.order.insert(buf.order.begin() + (long) std::min(at, buf.order.size()), text);
			buf.byKey.emplace(std::move(text), std::move(resp));
			responsesRecorded++;
		};

		if (!pinned.empty()) {
			recordFinished(pinned);
			return;
		}

		// The message is settled here and now -- rendered as it reads at this moment, with its
		// display-once state committed. ADRIFT does the same: AttemptToExecuteSubTask calls the
		// description's ToString before the task's actions run, and that call is what marks a
		// DisplayOnce part shown (the bTestingOutput guard it sets straight afterwards is for the
		// descriptions reached from *within* the message, not the message itself). So which parts
		// a message is made of is decided when the task speaks, and nothing later in the turn can
		// revise it. Rendering it at flush time instead let an action of the *calling* task rewrite
		// it after the fact -- something the before/after comparison around this task's own actions
		// cannot see: Grandma's tutorial line is executed by the "talk" task, which then increments
		// the very counter that chooses between its two variants, so the flush read the next line.
		std::string evaluated = MutableDescription(task->GetCompletionMsg())->BuildAndCommit();
		if (!evaluated.empty()) anyText = true;
		// Nothing to say: record nothing (mirrors ADRIFT's bHasOutput filter).
		if (evaluated.empty()) return;

		// Held for the end-of-command flush rather than printed now. An aggregating task is keyed
		// on the *unevaluated* text, so runs differing only in their bound references (a
		// multi-object command, a SetTasks FOR loop) collapse into one response -- which the flush
		// re-renders naming them all. Every other task is keyed on the finished text, keeping
		// distinct objects on separate lines, and is printed exactly as rendered here.
		std::string key = task->AggregatesOutput() ? desc->BuildRawKey() : evaluated;
		ResponseBuffer &buf = *activeResponseBuffer;
		auto it = buf.byKey.find(key);
		if (it == buf.byKey.end()) {
			AggregatedResponse resp;
			resp.descr = task->GetCompletionMsg();
			resp.pinnedText = evaluated;
			resp.refSnapshot = currentRefs;
			buf.order.insert(buf.order.begin() + (long) std::min(at, buf.order.size()), key);
			buf.byKey.emplace(std::move(key), std::move(resp));
			responsesRecorded++;
		} else {
			// Same message as an earlier run this command: merge in any reference whose value differs
			// from what was first recorded, so the flush can render the collapsed runs as one list.
			AggregatedResponse &resp = it->second;
			for (const auto &[name, val] : currentRefs) {
				auto mit = resp.mergedRefs.find(name);
				if (mit != resp.mergedRefs.end()) {
					auto &keys = mit->second;
					if (std::find(keys.begin(), keys.end(), val) == keys.end())
						keys.push_back(val);
					continue;
				}
				auto sit = resp.refSnapshot.find(name);
				if (sit != resp.refSnapshot.end() && sit->second == val)
					continue;  // unchanged from the snapshot -- nothing to merge for this name
				// First divergence for this name: seed the list with the snapshot's original value
				// (if any) so it is included alongside the new one.
				std::vector<std::string> keys;
				if (sit != resp.refSnapshot.end())
					keys.push_back(sit->second);
				keys.push_back(val);
				resp.mergedRefs.emplace(name, std::move(keys));
			}
		}
	};
	// The message is emitted (or, during a command, recorded into the response buffer -- see emit)
	// before or after this task's actions per its MessagePlacement. Ordering the emit around
	// RunActions this way keeps a Before message ahead of, and an After message behind, whatever a
	// nested "Execute" action itself emits/records -- so the buffer's insertion order, which the
	// end-of-command flush preserves, still reflects each message's place in the action sequence.
	// A task whose actions "Execute" another task has spoken through it, even with no message of its
	// own -- ADRIFT threads that back as ExecuteActions' bTaskHasOutputNew, and it is what stops the
	// search through this task's Specific siblings and the lower-priority tasks below it. (Alyas:
	// "pour water in brazier" is answered by a silent Specific override that executes the scoring
	// task; without this the sibling below it runs too and adds "The flask is empty.")
	auto runActionsAndNoteOutput = [&] {
		if (!runActions) return;
		const uint64_t before = responsesRecorded;
		// give the frontend a chance to attend to its business, in case we have a chain of
		// tasks executing each other for a while
		frontend->PumpEvents();
		task->RunActions();
		if (responsesRecorded != before) anyText = true;
	};

	if (!msgFirst) {
		// An After message is read once the task has completed and its actions have run -- which is
		// also the order ADRIFT sets task.Completed in, ahead of both.
		task->MarkCompleted();
		runActionsAndNoteOutput();
		emit(std::string());
		return anyText;
	}

	// Out of a command there is nothing buffering the output, so a Before message is simply printed
	// before the actions run: that is both its place in the transcript and, being read first, the
	// reading of the world the pre/post comparison below exists to preserve. (An event that narrates
	// something and then moves the player must narrate first and describe the new room second.)
	if (!activeResponseBuffer) {
		emit(std::string());
		task->MarkCompleted();
		runActionsAndNoteOutput();
		return anyText;
	}

	// A Before message describes the world as it was when the task fired, so ADRIFT reads it once
	// ahead of the actions, runs them, and reads it again: if the actions changed what it says, the
	// earlier reading is what the player gets. That is what makes "(from %objects%.Parent.Name)"
	// name where the object came from rather than the player who now holds it. The slot it would
	// have taken is remembered too, so it still lands ahead of whatever the actions record.
	//
	// Read before MarkCompleted, as ADRIFT reads it before setting task.Completed: a message part
	// gated on "this task must be complete" belongs to the *next* run, not the one completing it.
	// (Alyas's soldier answers "Bugger orf!" the first time and "no desire to converse" after.) That
	// makes completion itself something the reading can depend on, so the reading happens whether or
	// not the task has actions to change anything else.
	std::string before;
	if (showText && task->GetCompletionMsg() != 0)
		before = GetDescription(task->GetCompletionMsg())->Build();
	size_t slot = activeResponseBuffer ? activeResponseBuffer->order.size() : SIZE_MAX;
	task->MarkCompleted();
	runActionsAndNoteOutput();
	std::string after;
	if (!before.empty())
		after = GetDescription(task->GetCompletionMsg())->Build();
	emit(before != after ? before : std::string(), slot);
	return anyText;
}

// Which of a task's alternate <Command> lines an argument-bearing Execute call is really about.
// Unlike the player-input path (MatchInput), there is no typed sentence to regex against here, so
// the arguments themselves are the only clue: pick the alternate whose %ref% count matches the
// number of arguments, breaking ties by how many arguments are of the kind the ref family expects
// (an Object argument for a %object%, a Character for a %character%; a %text%/other family accepts
// anything). Falls back to the first line when nothing matches, preserving the no-argument case.
static size_t PickCommandAlternate(Game *g,
                                   const std::vector<std::vector<std::string>> &coding,
                                   const std::vector<std::string> &args) {
	size_t best = 0;
	int bestScore = -1;
	for (size_t i = 0; i < coding.size(); i++) {
		if (coding[i].size() != args.size())
			continue;
		int score = 0;
		for (size_t j = 0; j < args.size(); j++) {
			const std::string &family = SplitRefName(coding[i][j]).family;
			const GameObj *ob = g->TryGetObject(args[j]);
			const bool isChar = AsCharacter(ob) != nullptr;
			const bool isObj = ob && !isChar;
			if (IsObjectPronounFamily(family) ? isObj
			    : IsCharacterPronounFamily(family) ? isChar
			    : true)  // direction/number/text and the like: no object kind to disagree with
				score++;
		}
		if (score > bestScore) {
			bestScore = score;
			best = i;
		}
	}
	return best;
}

void Game::ExecuteTaskByKey(const std::string &key, const std::vector<std::string> &args) {
	Task *task = GetTask(key);
	if (!task) return;  // unknown task key: nothing to do

	// With explicit arguments, bind the alternate Command line that actually fits them rather than
	// blindly taking the first (a task's alternates can differ in reference count and kind). With no
	// arguments there is nothing to fit against, so the first line stands in as before.
	static const std::vector<std::string> kNoRefs;
	const auto &coding = task->GetGroupCoding();
	const std::vector<std::string> &refTokens =
		coding.empty() ? kNoRefs
		: args.empty() ? coding.front()
		: coding[PickCommandAlternate(this, coding, args)];

	// An Execute action that supplies arguments is naming the objects the called task is to act
	// on, in place of anything the player's own command referred to. Bind them to that task's
	// %ref%s for the duration of the call and put the caller's references back afterwards --
	// including if the call throws, since the turn may still be reported on.
	struct RefGuard {
		Game *g;
		bool active;
		std::unordered_map<std::string, std::string> saved;
		~RefGuard() { if (active) g->currentRefs = std::move(saved); }
	} guard{this, !args.empty(), {}};

	if (!args.empty()) {
		guard.saved = currentRefs;
		currentRefs.clear();
		for (size_t i = 0; i < args.size() && i < refTokens.size(); i++) {
			auto [family, suffix] = SplitRefName(refTokens[i]);
			currentRefs[Util::CanonicalizeRefName(refTokens[i])] = args[i];
			// Library restrictions address references generically, so the called task's own
			// restrictions are as likely to say "ReferencedObject" as "%object%".
			std::string alias = GenericAliasFamily(family);
			if (!alias.empty())
				currentRefs[Util::CanonicalizeRefName(alias + suffix)] = args[i];
		}
	}

	auto result = task->CheckRestrictions();
	if (!result.first) {
		if (result.second != 0)
			EmitFailureText(MutableDescription(result.second)->BuildAndCommit());
		return;
	}
	// A task reached through an "Execute" action still gets its Specific overrides applied,
	// exactly as a task the player typed does; ADRIFT runs both through the same path.
	RunTaskWithSpecifics(task, refTokens);
}

void Game::NotePlayerArrived(const std::string &locationKey) {
	auto found = staticData->systemTasksByLocation.find(locationKey);
	if (found == staticData->systemTasksByLocation.end()) return;
	for (const Task *t : found->second) {
		// A task that has run its course cannot run again, so there is no sense lining it up.
		// (RunTriggeredTasks would find this out for itself, but there is no point queueing
		// something only to throw it away.)
		if (t->Completed() && !t->IsRepeatable()) continue;
		triggeredTasks.push_back(t->Key());
	}
}

void Game::RunTriggeredTasks() {
	// Bounded rather than trusted: one of these tasks may well move the player again, lining up
	// more, which is the point -- but a pair of repeatable tasks that move the player back and
	// forth between each other's trigger locations would otherwise never let go.
	size_t ran = 0;
	while (!triggeredTasks.empty()) {
		if (++ran > kMaxTriggeredTasks) {
			triggeredTasks.clear();
			break;
		}
		std::string key = std::move(triggeredTasks.front());
		triggeredTasks.pop_front();
		ResponseScope scope(this);
		ExecuteTaskByKey(key);
	}
}

void Game::ExecuteMatchedTask(Task *general) {
	// Whatever this command named becomes the antecedent for "it"/"them"/"him"/"her" in whatever
	// the player types next. Every caller reaches this point with currentMatchedRefTokens/
	// currentRefs/currentRefLists fully resolved (past any disambiguation), so this is the one
	// place that needs to do the noting.
	UpdatePronounAntecedents();

	// Collect this command's completion messages so they can be aggregated and flushed together at
	// the end -- see RunTaskAndCapture / FlushResponseBuffer. The guard flushes on every exit path
	// (including the odometer's early returns) and restores any enclosing buffer.
	ResponseBuffer buffer;
	ResponseBuffer *prevBuffer = activeResponseBuffer;
	activeResponseBuffer = &buffer;
	struct BufferGuard {
		Game *g;
		ResponseBuffer *buffer;
		ResponseBuffer *prev;
		~BufferGuard() {
			// A destructor is noexcept, so a throw from inside the flush -- a message referring to
			// something that isn't there, say -- would take the process down rather than reaching
			// the backstop in ProcessInput. Losing the rest of this command's output is bad; losing
			// the session is worse. The buffer is restored either way.
			try {
				g->FlushResponseBuffer(*buffer);
			} catch (const std::exception &e) {
				LogError(std::string("Failed to print a command's responses: ") + e.what());
			}
			g->activeResponseBuffer = prev;
		}
	} bufferGuard{this, &buffer, prevBuffer};

	// "Take the plates and the ration bar" is two takes: ADRIFT runs the whole task once per thing
	// a plural reference named, so each gets its own restrictions checked and its own Specific
	// overrides applied. The references are bound to one combination at a time, cycling through
	// every combination when a command carries more than one plural reference.
	// A sweeping ALL command swallows its refusals (see EmitFailureText); if nothing it named could
	// be acted on at all, the task's own fail-override text stands in for the lot.
	const bool sweeping = !currentAllRefs.empty();
	bool anythingWorked = false;
	struct FailOverrideGuard {
		Game *g; Task *general; const bool &sweeping; const bool &worked;
		~FailOverrideGuard() {
			if (!sweeping || worked || general->GetFailOverrideMsg() == 0) return;
			g->OutputFiltered(g->MutableDescription(general->GetFailOverrideMsg())->BuildAndCommit());
		}
	} failGuard{this, general, sweeping, anythingWorked};

	std::vector<size_t> indices(currentRefLists.size(), 0);
	for (;;) {
		for (size_t i = 0; i < currentRefLists.size(); i++)
			BindReference(currentRefs, currentRefLists[i].first, currentRefLists[i].second[indices[i]]);

		auto parentResult = general->CheckRestrictions();
		if (!parentResult.first) {
			EmitFailureText(parentResult.second != 0
				? MutableDescription(parentResult.second)->BuildAndCommit()
				: std::string("You can't do that right now.\n"));
		} else {
			anythingWorked = true;
			RunTaskWithSpecifics(general, currentMatchedRefTokens);
		}

		// Advance to the next combination, odometer-style; stop once they have all been run.
		size_t pos = currentRefLists.size();
		while (pos > 0) {
			--pos;
			if (++indices[pos] < currentRefLists[pos].second.size()) break;
			indices[pos] = 0;
			if (pos == 0) return;
		}
		if (currentRefLists.empty()) return;
	}
}

void Game::FlushResponseBuffer(ResponseBuffer &buffer) {
	for (const std::string &key : buffer.order) {
		AggregatedResponse &resp = buffer.byKey.at(key);
		// Every message arrives here already rendered -- see emit() -- and is printed as it stands.
		// The one thing that can still change it is a reference that varied across collapsed runs:
		// an aggregating task's "Ok, you take %objects%.Name" has to come out naming all of them,
		// so a response that really did merge something is built once more with those references
		// overlaid as a pipe-joined key list, which %objects%.Name and %TheObject[...]% expand to
		// "the ball and the box". That rebuild deliberately does not commit: each run settled the
		// description's display-once state when it spoke, and consuming it again here would mark
		// parts shown that the player never saw.
		std::string text = resp.pinnedText;
		if (!resp.mergedRefs.empty() && resp.descr != 0) {
			std::unordered_map<std::string, std::string> savedRefs = std::move(currentRefs);
			currentRefs = resp.refSnapshot;
			for (const auto &[name, keys] : resp.mergedRefs) {
				if (keys.size() < 2) continue;
				currentRefs[name] = JoinKeys(keys);
			}
			std::string merged = GetDescription(resp.descr)->Build();
			currentRefs = std::move(savedRefs);
			if (!merged.empty()) text = std::move(merged);
		}
		// Record it against the turn-wide set so a later out-of-command message (a triggered task or
		// event this turn) with the same text stays suppressed, as it was before buffering existed.
		if (!text.empty() && completionMessagesThisTurn.insert(text).second)
			OutputFiltered(std::move(text));
	}
}

void Game::RecordResponse(std::string text) {
	if (text.empty()) return;
	responsesRecorded++;
	if (!activeResponseBuffer) {
		OutputFiltered(std::move(text));
		return;
	}
	// Already-evaluated text (a restriction-failure message, say) still has to queue behind the
	// completion messages recorded so far rather than jumping ahead of them: within a command,
	// everything the player sees is ordered by the buffer, not by when it happened to be built.
	// Keyed by the text itself, as ADRIFT's AddResponse keys an evaluated message.
	ResponseBuffer &buf = *activeResponseBuffer;
	if (buf.byKey.find(text) != buf.byKey.end()) return;
	AggregatedResponse resp;
	resp.descr = 0;
	resp.pinnedText = text;
	buf.order.push_back(text);
	buf.byKey.emplace(std::move(text), std::move(resp));
}

bool Game::RunTaskWithSpecifics(Task *general, const std::vector<std::string> &refTokens) {
	// Running a child goes back through here rather than straight to RunTaskAndCapture, because a
	// Specific task may itself be the parent of further Specific tasks: the standard library's
	// "Take objects from location" is a Specific override of "Take objects", and a game hangs its
	// own "get the mouse nest" override off *that*. ADRIFT recurses the same way (its child loop
	// calls AttemptToExecuteTask, the same entry point it used for the parent).
	auto runChild = [&](Task *child) {
		return GetSpecificChildren(child->Key()).empty()
			? RunTaskAndCapture(child) : RunTaskWithSpecifics(child, refTokens);
	};

	// The general task's restrictions have passed; see whether any of its Specific children apply.
	// Children are visited in priority order and, in ADRIFT, more than one may run: the chain
	// stops at the first child that both ran (or spoke up about failing) and had something to
	// say, unless that child is explicitly marked "continue to execute lower priority tasks".
	// So "before crawl through duct" (which just moves the player in) runs, and then the real
	// "crawl through duct" below it still gets its turn.
	std::vector<Task *> afterChildren;

	bool showParentText = true;
	bool runParentActions = true;
	// Whether anything at all was said, counting the children as well as the parent -- the answer a
	// nested call owes its own caller, which uses it to decide whether to keep looking further down.
	bool anyOutput = false;

	for (Task *child : GetSpecificChildren(general->Key())) {
		if (!SpecificTaskMatches(child, refTokens)) continue;
		auto overrideType = child->GetOverrideType();
		if (overrideType.Has(Task::OverrideType::AfterParent)) {
			afterChildren.push_back(child);
			continue;
		}
		auto childResult = child->CheckRestrictions();
		bool childHadSomethingToSay = false;
		if (childResult.first) {
			childHadSomethingToSay = runChild(child);
			// A child that ran suppresses whichever parts of the parent it says it replaces,
			// whether or not it printed anything.
			if (!overrideType.Has(Task::OverrideType::ParentText)) showParentText = false;
			if (!overrideType.Has(Task::OverrideType::ParentActions)) runParentActions = false;
		} else if (childResult.second != 0) {
			// The child failed, but produced restriction-failure text of its own: that takes
			// precedence over the parent, same as if the child had passed.
			std::string text = MutableDescription(childResult.second)->BuildAndCommit();
			childHadSomethingToSay = !text.empty();
			EmitFailureText(std::move(text));
			if (childHadSomethingToSay) {
				if (!overrideType.Has(Task::OverrideType::ParentText)) showParentText = false;
				if (!overrideType.Has(Task::OverrideType::ParentActions)) runParentActions = false;
			}
		}
		// A child that neither ran nor produced any message is treated as if it hadn't matched at
		// all, and we keep looking; so is one that ran silently.
		anyOutput = anyOutput || childHadSomethingToSay;
		if (childHadSomethingToSay && !child->AlwaysContinues())
			break;
	}

	anyOutput = RunTaskAndCapture(general, showParentText, runParentActions) || anyOutput;

	for (Task *child : afterChildren) {
		auto childResult = child->CheckRestrictions();
		bool childHadSomethingToSay = false;
		if (childResult.first) {
			childHadSomethingToSay = runChild(child);
		} else if (childResult.second != 0) {
			std::string text = MutableDescription(childResult.second)->BuildAndCommit();
			childHadSomethingToSay = !text.empty();
			EmitFailureText(std::move(text));
		}
		anyOutput = anyOutput || childHadSomethingToSay;
		if (childHadSomethingToSay && !child->AlwaysContinues())
			break;
	}
	return anyOutput;
}

std::vector<std::string> Game::NarrowByAnswer(const std::vector<std::string> &candidates, const std::string &answer) {
	auto answerWords = Util::SplitString(frontend->StrToLowerCase(answer), " ");
	std::vector<std::string> result;
	for (const auto &key : candidates) {
		const GameObj *ob = TryGetObject(key);
		if (!ob) continue;
		bool matchesAll = true;
		for (const auto &word : answerWords) {
			// Blank tokens (double spaces) and a literal "the" never count against a candidate --
			// the same leniency the reference PossibleKeys affords; every other word must name it.
			if (word.empty() || word == "the") continue;
			if (!ob->MatchesNameWord(word)) { matchesAll = false; break; }
		}
		if (matchesAll) result.push_back(key);
	}
	return result;
}

void Game::DisplayAmbiguityQuestion(const RefMatchInfo &info) {
	// "Which <word>?" -- the noun the candidates share. Prefer the first word of the player's own
	// phrasing that every candidate answers to; fall back to the raw text if none qualifies.
	std::string word = info.raw;
	for (const auto &w : Util::SplitString(frontend->StrToLowerCase(info.raw), " ")) {
		if (w.empty()) continue;
		bool inAll = true;
		for (const auto &key : info.candidates) {
			const GameObj *ob = TryGetObject(key);
			if (!ob || !ob->MatchesNameWord(w)) { inAll = false; break; }
		}
		if (inAll) { word = w; break; }
	}
	// "The red ball or the green ball." -- GetDisplayName(true) yields the lowercase definite form;
	// OutputFiltered's AutoCapitalize raises the leading article, it following the "? " above.
	std::string list;
	for (size_t i = 0; i < info.candidates.size(); i++) {
		if (i != 0) list += (i + 1 == info.candidates.size()) ? " or " : ", ";
		const GameObj *ob = TryGetObject(info.candidates[i]);
		list += ob ? ob->GetDisplayName(true) : info.candidates[i];
	}
	OutputFiltered("Which " + word + "? " + list + ".\n");
}

// The first reference slot, in `refTokens` order, that still matches more than one object -- a
// whole singular reference (from `refMatches`) or one item of a plural reference (from
// `itemMatches`, e.g. "the ball" in "the plates and the ball"). Returns the RefMatchInfo to ask
// about, or nullptr when nothing is ambiguous. `outToken` is the raw reference token; `outItemIdx`
// is the plural item's index, or -1 for a singular reference.
Game::RefMatchInfo *Game::FirstAmbiguousSlot(
		const std::vector<std::string> &refTokens,
		std::unordered_map<std::string, RefMatchInfo> &refMatches,
		std::unordered_map<std::string, std::vector<RefMatchInfo>> &itemMatches,
		std::string &outToken, int &outItemIdx) {
	for (const auto &token : refTokens) {
		std::string canon = Util::CanonicalizeRefName(token);
		auto sit = refMatches.find(canon);
		if (sit != refMatches.end() && sit->second.candidates.size() > 1) {
			outToken = token;
			outItemIdx = -1;
			return &sit->second;
		}
		auto pit = itemMatches.find(canon);
		if (pit != itemMatches.end()) {
			for (size_t j = 0; j < pit->second.size(); j++) {
				if (pit->second[j].candidates.size() > 1) {
					outToken = token;
					outItemIdx = (int) j;
					return &pit->second[j];
				}
			}
		}
	}
	return nullptr;
}

bool Game::BeginDisambiguationIfNeeded(Task *chosen) {
	std::string token;
	int itemIdx;
	RefMatchInfo *amb = FirstAmbiguousSlot(currentMatchedRefTokens, currentRefMatches,
	                                       currentRefItemMatches, token, itemIdx);
	if (!amb) return false;
	// Some reference matched several objects (or one item of a plural reference did): hold the whole
	// command, ask about that slot, and let the player's next line resolve it (see ResolveDisambiguation).
	pendingDisambig = PendingDisambig{chosen, currentMatchedRefTokens, currentRefs,
	                                 currentRefMatches, currentRefLists, currentRefItemMatches};
	DisplayAmbiguityQuestion(*amb);
	return true;
}

// Record a plural reference's item choice into the held command's state: update that item's slot in
// pd.refLists (which ExecuteMatchedTask's per-item odometer reads), and, when it is the first item,
// the reference's provisional single binding too (which is item 0 -- see CaptureReferences).
void Game::SetPluralItemChoice(PendingDisambig &pd, const std::string &token,
                               int itemIdx, const std::string &chosenKey) {
	for (auto &entry : pd.refLists) {
		if (entry.first == token) {
			if (itemIdx >= 0 && (size_t) itemIdx < entry.second.size())
				entry.second[itemIdx] = chosenKey;
			break;
		}
	}
	if (itemIdx == 0)
		BindReference(pd.refs, token, chosenKey);
}

void Game::ResolveDisambiguation(const std::string &answer) {
	PendingDisambig &pd = *pendingDisambig;

	// The slot we are currently asking about is the first one still matching several objects.
	std::string ambToken;
	int ambItemIdx;
	RefMatchInfo *ambInfo = FirstAmbiguousSlot(pd.refTokens, pd.refMatches, pd.itemMatches,
	                                           ambToken, ambItemIdx);
	if (!ambInfo) { pendingDisambig.reset(); return; }  // nothing left to ask; shouldn't happen

	auto narrowed = NarrowByAnswer(ambInfo->candidates, answer);
	if (!narrowed.empty()) {
		// The answer picked out one or more of the candidates: adopt it as this slot's value.
		ambInfo->candidates = std::move(narrowed);
		if (ambItemIdx < 0)
			BindReference(pd.refs, ambToken, ambInfo->candidates.front());
		else
			SetPluralItemChoice(pd, ambToken, ambItemIdx, ambInfo->candidates.front());

		// Still ambiguous (the answer narrowed but didn't settle it)? Keep asking about this one.
		if (ambInfo->candidates.size() > 1) {
			DisplayAmbiguityQuestion(*ambInfo);
			return;
		}
		// Settled -- but another slot of the same command may still be ambiguous.
		std::string nextToken;
		int nextItemIdx;
		if (RefMatchInfo *next = FirstAmbiguousSlot(pd.refTokens, pd.refMatches, pd.itemMatches,
		                                            nextToken, nextItemIdx)) {
			DisplayAmbiguityQuestion(*next);
			return;
		}
		// Everything resolved: run the held command, now as a real turn.
		Task *task = pd.task;
		currentRefs = std::move(pd.refs);
		currentMatchedRefTokens = std::move(pd.refTokens);
		// Restored along with the references: answering may have run FindMatchingTask (when an
		// earlier answer turned out to name no candidate), which repopulates the live lists.
		currentRefLists = std::move(pd.refLists);
		currentRefItemMatches = std::move(pd.itemMatches);
		pendingDisambig.reset();
		SaveUndo();
		ExecuteMatchedTask(task);
		RunTriggeredTasks();
		TurnTick();
		return;
	}

	// The answer named none of the candidates. Per the hybrid rule: if it is itself a command the
	// game understands, abandon the disambiguation and run it; otherwise (mere gibberish) re-ask.
	RefMatchInfo askAgain = *ambInfo;  // the re-ask path clears nothing, but copy to be safe
	currentCommand = ApplySynonyms(SubstitutePronouns(answer));
	Task *chosen = FindMatchingTask();
	if (chosen) {
		pendingDisambig.reset();
		if (BeginDisambiguationIfNeeded(chosen)) return;  // the replacement command is itself ambiguous
		SaveUndo();
		ExecuteMatchedTask(chosen);
		RunTriggeredTasks();
		TurnTick();
		return;
	}
	// A system command both tests and runs in one call, so the pending state is cleared first and
	// put back if it turns out not to have been one after all. (This used to be about UNDO and
	// RESTART replacing `this` outright; they work in place now, but abandoning the disambiguation
	// before running the command is still the right order.)
	auto savedPending = std::move(pendingDisambig);
	pendingDisambig.reset();
	if (AttemptMatchSystemCommand()) return;  // matched and ran, abandoning the disambiguation
	pendingDisambig = std::move(savedPending);
	DisplayAmbiguityQuestion(askAgain);
}

void Game::ProcessInput(const std::string &s) {
	// Hold off any real-time tick until this command is finished with. A frontend that stops to
	// ask the player something (a modal question, a file dialog) will keep servicing its timer
	// while this call is still on the stack, and a command half-applied is not a state the world
	// should be allowed to move on from. An object, not a bare assignment, so that it also unwinds
	// on the paths out of here that destroy the Game (RESTART, UNDO) or throw.
	struct InputGuard {
		InputGuard() { inputInFlight = true; }
		~InputGuard() { inputInFlight = false; }
	} guard;

	// Every line the player types is a turn as far as %Turns% is concerned -- see turnCount --
	// counted before the line is acted on, so a message this command prints reports the command
	// it belongs to rather than the one before it.
	turnCount += 1;
	// Every line the player types starts a fresh block of output, whether it turns out to be a
	// command or the answer to a question we asked; there is nothing to separate its first
	// message from.
	turnHasOutput = false;
	completionMessagesThisTurn.clear();
	// A character is only pronominalised once the player has seen them named earlier in the same
	// turn (see DisplayCharacterName) -- so that tracking resets here too.
	charactersMentionedThisTurn.clear();

	// The game has ended and is waiting on the final question: nothing else has any state left to
	// act on, so skip straight past disambiguation and ordinary tasks and try only the four
	// commands that question offers. (Careful: this can delete `this`, same as below.)
	if (!gameHasBegun) {
		currentCommand = frontend->StrToLowerCase(s);
		if (AttemptMatchEndOfGameCommand()) return;
		// Mirrors ADRIFT's own wording for the same situation.
		OutputFiltered("Please give one of the answers above.\n");
		return;
	}

	// A question we asked the player ("Which ball?") is still open: this line is their answer, not
	// a fresh command. Route it to the resolver, which runs the held command once the reference is
	// pinned down (or, if the answer is really a different command, runs that instead).
	// (Careful: like the system-command path below, this can delete `this`.)
	if (pendingDisambig) {
		ResolveDisambiguation(s);
		return;
	}

	std::string typed = frontend->StrToLowerCase(s);
	// AGAIN (or G) stands in for whatever was typed last, and says which command it took that to
	// be. The recalled text is stored with its pronouns already resolved, so "take it" followed by
	// G takes the same thing again rather than whatever "it" has come to mean since -- and so that
	// a second G repeats the same command rather than itself.
	const std::string trimmed = NormalizeSystemCommand(typed);
	if ((trimmed == "again" || trimmed == "g") && !lastCommand.empty()) {
		OutputFiltered("<c>(" + lastCommand + ")</c>\n");
		typed = lastCommand;
	} else {
		typed = SubstitutePronouns(std::move(typed));
		lastCommand = typed;
	}
	currentCommand = ApplySynonyms(typed);

	Task *chosenTask = FindMatchingTask();

	if (!chosenTask) {
		// No match, attempt to read this as a system command ...
		// (Careful: a system command may well have deleted `this` by the time this returns.)
		if (AttemptMatchSystemCommand()) return;
		// ... then, if we asked "Launch what?" last time, read this line as the answer to it and
		// try the whole thing again. ADRIFT's NotUnderstood does this first of all, and a verb is
		// only ever remembered when it matched some task's command but for a missing reference, so
		// the retry is looking for that task with its reference finally supplied.
		if (!rememberedVerb.empty()) {
			const std::string original = currentCommand;
			// Simple concatenation, with no second pass of the synonym table: ADRIFT's retry goes
			// through EvaluateInput with a non-zero minimum priority, and the synonym pass sits
			// inside that function's iMinimumPriority = 0 block. Both halves have been through it
			// once already, and running a chain of synonyms over them twice would not be the same.
			currentCommand = rememberedVerb + " " + currentCommand;
			rememberedVerb.clear();
			if (Task *withVerb = FindMatchingTask()) {
				if (BeginDisambiguationIfNeeded(withVerb)) return;
				SaveUndo();
				ExecuteMatchedTask(withVerb);
				RunTriggeredTasks();
				TurnTick();
				MarkVisibleThingsSeen();
				return;
			}
			// Not that either: fall through with the original wording restored, so the rejection
			// below is about what the player actually typed.
			currentCommand = original;
		}
		// ... and, failing that, try ADRIFT's two more specific rejections -- a bare verb missing
		// its object ("Launch what?") and input naming a visible thing no task's command matched
		// ("I don't understand what you want to do with the rock.") -- before falling back to the
		// generic one.
		if (PromptForIncompleteVerb()) return;
		if (DescribeUnmatchedThing()) return;
		OutputFiltered("I didn't understand that sentence.\n");
		return;
	}
	// Understood: whatever question was outstanding has been overtaken by a real command.
	rememberedVerb.clear();

	// The command may refer to an object ambiguously ("take ball" with a red and a green ball both
	// present). If so, ask the player which they mean and hold the command until their next line
	// answers -- asking is not a turn, so no undo state is recorded and the world does not move on.
	if (BeginDisambiguationIfNeeded(chosenTask)) return;

	// Whatever changes the game world counts as a turn and gets a state recorded for UNDO to
	// return to. That is every task, and -- among the system commands -- WAIT alone, which
	// records its own (see AttemptMatchSystemCommand); undoing a SAVE would be meaningless.
	// The state goes in before the task runs, so that what UNDO comes back to is the game as
	// it stood when the player typed the command.
	SaveUndo();
	ExecuteMatchedTask(chosenTask);
	// If the command took the player somewhere, whatever waits there happens now -- after the
	// command has finished having its effects, and before the world takes its turn. ADRIFT
	// sequences these three the same way.
	RunTriggeredTasks();
	// The command is done; the world moves on. ADRIFT skips this for its System-type tasks, but
	// only General tasks can ever be matched from player input here, so there is nothing to skip.
	TurnTick();
	// Last, as ADRIFT's PrepareForNextTurn does: whatever the turn brought into view has now been
	// seen, and the "has been seen by" restrictions the standard library leans on will pass on the
	// next command.
	MarkVisibleThingsSeen();
}

bool Game::AttemptMatchEndOfGameCommand() {
	const std::string cmd = NormalizeSystemCommand(currentCommand);

	if (cmd == "restart") {
		Restart();  // in place: `this` and everything in it survives
		return true;
	}
	if (cmd == "restore") {
		// Restore() reports its own failures, and a failed restore rolls the game back to the
		// state recorded before it started meddling.
		if (Restore())
			Game::Get()->OutputFiltered("Restored.\n");
		return true;
	}
	if (cmd == "quit") {
		if (!frontend->AskYesNo("Are you sure you want to quit?"))
			return true;
		// Signal the end of play before handing over, so that a frontend asking GameIsOngoing()
		// or driving TimeTick() from within (or just after) QuitGame() gets a truthful answer and
		// stops moving the world. Both flags: QUIT reaches here whether the game is still running
		// (gameHasBegun) or already ended and waiting on this very question (sessionActive alone).
		gameHasBegun = false;
		sessionActive = false;
		frontend->QuitGame();
		return true;
	}
	if (cmd == "undo") {
		if (!UndoAvailable()) {
			OutputFiltered("Sorry, <c>undo</c> is not currently available.\n");
			return true;
		}
		RestoreUndo();  // in place, like RESTART
		OutputFiltered("Undone.\n");
		return true;
	}
	return false;
}

bool Game::AttemptMatchSystemCommand() {
	// May end the session (QUIT), but no longer replaces the Game instance -- see above.
	if (AttemptMatchEndOfGameCommand()) return true;

	const std::string cmd = NormalizeSystemCommand(currentCommand);

	// ADRIFT remembers the file a game was last saved to and quietly overwrites it on every
	// subsequent SAVE, reserving SAVE AS for choosing a new one. We always ask the player where
	// the save should go, which leaves the two commands with nothing to tell them apart.
	if (cmd == "save" || cmd == "save as" || cmd == "saveas") {
		OutputFiltered(Save() ? "Saved.\n" : "Save cancelled.\n");
		return true;
	}
	if (cmd == "wait" || cmd == "z") {
		// Alone among the commands here, WAIT changes the game: it is the player choosing to
		// spend `waitTurns` turns doing nothing while the world carries on around them. That
		// makes it a turn like any other, so it records a state of its own -- without one, an
		// UNDO after a WAIT would skip back past whatever the player did *before* the wait,
		// losing a command they never asked to undo. ADRIFT doesn't do this, but it only ever
		// treated WAIT as a system command for want of somewhere better to put it (its source
		// marks it as something that ought to have been an ordinary task).
		// Recorded before the output, since producing text can mark a "display once" segment
		// as shown, which is itself game state.
		SaveUndo();
		OutputFiltered("Time passes...\n");
		// The player has chosen to spend `waitTurns` turns doing nothing, so the world gets that
		// many turns to carry on around them while they do it.
		for (uint32_t i = 0; i < staticData->waitTurns; i++)
			TurnTick();
		return true;
	}

	// SAVE <filename> and RESTORE <filename> are deliberately absent: picking a file is the
	// frontend's business, not ours.
	return false;
}

}  // namespace Starlane
