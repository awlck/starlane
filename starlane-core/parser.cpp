//
// Created by Adrian Welcker on 11.07.23.
//

#include "game.h"

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
	for (auto &c : result.family) c = (char) std::tolower((unsigned char) c);
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
}  // anonymous namespace

std::string Game::ApplySynonyms(std::string s) {
	for (const auto &it: staticData->synonyms) {
		for (const auto &f: it.second->GetFrom()) {
			size_t n;
			while ((n = s.find(f)) != std::string::npos)
				s.replace(n, f.size(), it.second->GetReplacement());
		}
	}
	return s;
}

std::vector<std::string> Game::MatchListForReference(const std::string &from, const std::string &refFamily) const {
	using namespace std::string_literals;
	ReferenceType rt;
	if (refFamily.substr(0, sizeof("object")-1) == "object"s) rt = ReferenceType::Object;
	else if (refFamily.substr(0, sizeof("character")-1) == "character"s) rt = ReferenceType::Character;
	else throw std::runtime_error("Unknown reference type in task: " + refFamily);

	std::vector<std::string> result;
	for (const auto &it : objects) {
		switch (rt) {
			case ReferenceType::Object:
				if (dynamic_cast<Character *>(it.second)) continue;
				break;
			case ReferenceType::Character:
				if (!dynamic_cast<Character *>(it.second)) continue;
				break;
		}
		if (std::regex_match(from, it.second->GetMatchExpr()))
			result.push_back(it.first);
	}

	// Narrow the name matches down by scope, preferring the narrowest scope that still
	// leaves us with at least one object: first things the player can currently see,
	// then things they have seen at some point. If neither yields anything, keep the
	// full list; the matched task's own restrictions will then produce a sensible
	// failure message (e.g. "You see no such thing.").
	const auto *player = dynamic_cast<const Character *>(GetPlayerChar());
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

bool Game::CaptureReferences(const std::vector<std::string> &refSpecs, const std::smatch &matches) {
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
			resolved = Util::CanonicalizeDirection(raw);
		} else {
			// Objects, characters, locations, items, and their plurals: resolve the raw text
			// to an actual game object.
			auto matchList = MatchListForReference(raw, family);
			if (matchList.empty()) return false;
			// TODO: proper disambiguation when multiple objects match (ADRIFT prompts the
			// player to clarify); for now, just take the first match.
			resolved = matchList.front();
		}

		// Store under the literal name from this task's own Command pattern (e.g. "%object1%"),
		// which is what that task's own restriction/message text will refer to it as...
		currentRefs[ref] = resolved;
		// ...as well as under ADRIFT's generic, position-based name (e.g. "ReferencedObject1"),
		// which is what library restrictions shared across many tasks use instead.
		std::string alias = GenericAliasFamily(family);
		if (!alias.empty())
			currentRefs[alias + suffix] = resolved;
	}
	return true;
}

Task *Game::FindMatchingTask(std::pair<bool, DescrRef> &eligible) {
	for (Task *task : staticData->prioOrderedTasks) {
		if (task->GetType() != Task::Type::General) continue;

		const auto &regexes = task->GetCmdRegexes();
		const auto &groupCoding = task->GetGroupCoding();
		for (size_t cmdIdx = 0; cmdIdx < regexes.size(); cmdIdx++) {
			std::smatch matches;
			if (!std::regex_match(currentCommand, matches, regexes[cmdIdx])) continue;

			// References must be captured *before* checking eligibility: restrictions
			// (e.g. "must have a route in %direction%") need to see the values the player
			// actually typed.
			currentRefs.clear();
			if (!CaptureReferences(groupCoding[cmdIdx], matches)) continue;

			eligible = task->Eligible();
			// Choose the first (highest-priority) task that tentatively passes restrictions,
			// or otherwise fails restrictions but wants to output some text because of it.
			// Ignore tasks that fail restrictions and have no associated message.
			if (eligible.first || eligible.second != 0) return task;
		}
	}
	return nullptr;
}

void Game::ProcessInput(const std::string &s) {
	currentCommand = ApplySynonyms(s);

	// TODO: deal with the two execution policies.
	std::pair<bool, DescrRef> eligible{false, 0};
	Task *chosenTask = FindMatchingTask(eligible);

	if (!chosenTask) {
		// No match, attempt to read this as a system command ...
		if (AttemptMatchSystemCommand()) return;
		// ... and, failing that, reject the command as unknown.
		OutputFiltered("I didn't understand that sentence.\n");
		return;
	}

	// output failure message if restrictions failed
	if (!eligible.first) {
		if (eligible.second != 0) {
			OutputFiltered(GetDescription(eligible.second)->Build());
		} else {
			OutputFiltered("You can't do that right now.\n");
		}
		return;
	}

	auto result = chosenTask->Execute();
	if (!result.first) {
		OutputFiltered(result.second != 0 ? GetDescription(result.second)->Build() : "You can't do that right now.\n");
		return;
	}
	if (result.second != 0)
		OutputFiltered(GetDescription(result.second)->Build());
}

bool Game::AttemptMatchSystemCommand() {
	return false;
}

}  // namespace Starlane
