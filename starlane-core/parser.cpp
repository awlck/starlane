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

bool CaseInsensitiveEq(const std::string &a, const std::string &b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++)
		if (std::tolower((unsigned char) a[i]) != std::tolower((unsigned char) b[i])) return false;
	return true;
}
}  // anonymous namespace

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

Task *Game::FindMatchingTask() {
	// Only used as a fallback under ExecutionPolicy::HighestPrioPassing: the first
	// failing-with-message match encountered, in case no task ever passes restrictions.
	Task *fallback = nullptr;
	std::vector<std::string> fallbackRefTokens;
	std::unordered_map<std::string, std::string> fallbackRefs;

	for (Task *task : staticData->prioOrderedTasks) {
		if (task->GetType() != Task::Type::General) continue;
		// A completed, non-repeatable task is not a candidate at all.
		if (task->Completed() && !task->IsRepeatable()) continue;

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
			}
		}
	}

	if (fallback) {
		currentRefs = std::move(fallbackRefs);
		currentMatchedRefTokens = std::move(fallbackRefTokens);
	}
	return fallback;
}

const std::vector<Task *> &Game::GetSpecificChildren(const std::string &generalKey) const {
	static const std::vector<Task *> kEmpty;
	auto it = staticData->specificChildren.find(generalKey);
	return it == staticData->specificChildren.end() ? kEmpty : it->second;
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
		if (spec.key.empty()) continue;  // wildcard: matches any value for this reference

		auto it = currentRefs.find(refTokens[i]);
		if (it == currentRefs.end()) return false;

		if (spec.type == Task::SpType::Text) {
			if (!CaseInsensitiveEq(it->second, spec.key)) return false;
		} else {
			const std::string &want = (spec.key == "%Player%" || spec.key == "Player") ? playerKey : spec.key;
			if (it->second != want) return false;
		}
	}
	return true;
}

void Game::RunTaskAndCapture(Task *task, bool showText, bool runActions) {
	task->MarkCompleted();
	bool msgFirst = task->GetMessagePlacement() == Task::MessagePlacement::Before;
	if (msgFirst && showText && task->GetCompletionMsg() != 0)
		OutputFiltered(GetDescription(task->GetCompletionMsg())->Build());
	// Actions run between (or after/before) the message output points above/below, so that a
	// nested "Execute" action's own output interleaves in true chronological order rather than
	// being buffered and flushed out of order relative to this task's own message.
	if (runActions) task->RunActions();
	if (!msgFirst && showText && task->GetCompletionMsg() != 0)
		OutputFiltered(GetDescription(task->GetCompletionMsg())->Build());
}

void Game::ExecuteTaskByKey(const std::string &key) {
	Task *task = GetTask(key);
	if (!task) return;  // unknown task key: nothing to do

	auto result = task->CheckRestrictions();
	if (!result.first) {
		if (result.second != 0)
			OutputFiltered(GetDescription(result.second)->Build());
		return;
	}
	RunTaskAndCapture(task);
}

void Game::ExecuteMatchedTask(Task *general) {
	auto parentResult = general->CheckRestrictions();
	if (!parentResult.first) {
		OutputFiltered(parentResult.second != 0 ? GetDescription(parentResult.second)->Build() : "You can't do that right now.\n");
		return;
	}

	// A general task's restrictions passed; see whether any of its Specific children apply.
	// At most one "before/override" child and one "after" child are ever considered -- the
	// first (highest-priority) one, in each group, whose per-reference constraints match.
	Task *beforeChild = nullptr;
	Task *afterChild = nullptr;
	for (Task *child : GetSpecificChildren(general->Key())) {
		if (!SpecificTaskMatches(child, currentMatchedRefTokens)) continue;
		if (child->GetOverrideType().Has(Task::OverrideType::AfterParent)) {
			if (!afterChild) afterChild = child;
		} else {
			if (!beforeChild) beforeChild = child;
		}
		if (beforeChild && afterChild) break;
	}

	bool showParentText = true;
	bool runParentActions = true;

	if (beforeChild) {
		auto overrideType = beforeChild->GetOverrideType();
		auto childResult = beforeChild->CheckRestrictions();
		bool childHadSomethingToSay = childResult.first;
		if (childResult.first) {
			RunTaskAndCapture(beforeChild);
		} else if (childResult.second != 0) {
			// The child failed, but produced restriction-failure text of its own: that takes
			// precedence over the parent, same as if the child had passed.
			OutputFiltered(GetDescription(childResult.second)->Build());
			childHadSomethingToSay = true;
		}
		// A child that neither ran nor produced any message is treated as if it hadn't
		// matched at all, and the parent proceeds completely normally.
		if (childHadSomethingToSay) {
			if (!overrideType.Has(Task::OverrideType::ParentText)) showParentText = false;
			if (!overrideType.Has(Task::OverrideType::ParentActions)) runParentActions = false;
		}
	}

	RunTaskAndCapture(general, showParentText, runParentActions);

	if (afterChild) {
		auto childResult = afterChild->CheckRestrictions();
		if (childResult.first) {
			RunTaskAndCapture(afterChild);
		} else if (childResult.second != 0) {
			OutputFiltered(GetDescription(childResult.second)->Build());
		}
	}
}

void Game::ProcessInput(const std::string &s) {
	currentCommand = ApplySynonyms(s);

	Task *chosenTask = FindMatchingTask();

	if (!chosenTask) {
		// No match, attempt to read this as a system command ...
		if (AttemptMatchSystemCommand()) return;
		// ... and, failing that, reject the command as unknown.
		OutputFiltered("I didn't understand that sentence.\n");
		return;
	}

	ExecuteMatchedTask(chosenTask);
}

bool Game::AttemptMatchSystemCommand() {
	return false;
}

}  // namespace Starlane
