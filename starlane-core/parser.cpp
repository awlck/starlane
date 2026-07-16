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

		// Store under the name from this task's own Command pattern (e.g. "%object1%"),
		// which is what that task's own restriction/message text will refer to it as...
		currentRefs[Util::CanonicalizeRefName(ref)] = resolved;
		// ...as well as under ADRIFT's generic, position-based name (e.g. "ReferencedObject1"),
		// which is what library restrictions shared across many tasks use instead.
		std::string alias = GenericAliasFamily(family);
		if (!alias.empty())
			currentRefs[Util::CanonicalizeRefName(alias + suffix)] = resolved;
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

		auto it = currentRefs.find(Util::CanonicalizeRefName(refTokens[i]));
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

void Game::ExecuteTaskByKey(const std::string &key, const std::vector<std::string> &args) {
	Task *task = GetTask(key);
	if (!task) return;  // unknown task key: nothing to do

	// TODO: a task with several alternate Command lines only ever gets its first one's
	// references considered here.
	static const std::vector<std::string> kNoRefs;
	const auto &coding = task->GetGroupCoding();
	const std::vector<std::string> &refTokens = coding.empty() ? kNoRefs : coding.front();

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
			OutputFiltered(GetDescription(result.second)->Build());
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
		ExecuteTaskByKey(key);
	}
}

void Game::ExecuteMatchedTask(Task *general) {
	auto parentResult = general->CheckRestrictions();
	if (!parentResult.first) {
		OutputFiltered(parentResult.second != 0 ? GetDescription(parentResult.second)->Build() : "You can't do that right now.\n");
		return;
	}
	RunTaskWithSpecifics(general, currentMatchedRefTokens);
}

void Game::RunTaskWithSpecifics(Task *general, const std::vector<std::string> &refTokens) {
	// The general task's restrictions have passed; see whether any of its Specific children apply.
	// At most one "before/override" child and one "after" child are ever considered -- the
	// first (highest-priority) one, in each group, whose per-reference constraints match.
	Task *beforeChild = nullptr;
	Task *afterChild = nullptr;
	for (Task *child : GetSpecificChildren(general->Key())) {
		if (!SpecificTaskMatches(child, refTokens)) continue;
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
	// Hold off any real-time tick until this command is finished with. A frontend that stops to
	// ask the player something (a modal question, a file dialog) will keep servicing its timer
	// while this call is still on the stack, and a command half-applied is not a state the world
	// should be allowed to move on from. An object, not a bare assignment, so that it also unwinds
	// on the paths out of here that destroy the Game (RESTART, UNDO) or throw.
	struct InputGuard {
		InputGuard() { inputInFlight = true; }
		~InputGuard() { inputInFlight = false; }
	} guard;

	currentCommand = ApplySynonyms(s);

	Task *chosenTask = FindMatchingTask();

	if (!chosenTask) {
		// No match, attempt to read this as a system command ...
		// (Careful: a system command may well have deleted `this` by the time this returns.)
		if (AttemptMatchSystemCommand()) return;
		// ... and, failing that, reject the command as unknown.
		OutputFiltered("I didn't understand that sentence.\n");
		return;
	}

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
}

bool Game::AttemptMatchSystemCommand() {
	const std::string cmd = NormalizeSystemCommand(currentCommand);

	if (cmd == "restart") {
		// Restart() puts a fresh copy of the game's starting state in our place and destroys
		// this instance, so there is nothing left here to say afterwards -- and nothing of
		// `this` left to say it with.
		Restart();
		return true;
	}
	if (cmd == "restore") {
		// Restore() reports its own failures, and a failed restore rolls the game back to a
		// snapshot taken before it started meddling -- which destroys this instance. So the
		// success message has to come from whatever instance is current afterwards.
		if (Restore())
			Game::Get()->OutputFiltered("Restored.\n");
		return true;
	}
	// ADRIFT remembers the file a game was last saved to and quietly overwrites it on every
	// subsequent SAVE, reserving SAVE AS for choosing a new one. We always ask the player where
	// the save should go, which leaves the two commands with nothing to tell them apart.
	if (cmd == "save" || cmd == "save as" || cmd == "saveas") {
		OutputFiltered(Save() ? "Saved.\n" : "Save cancelled.\n");
		return true;
	}
	if (cmd == "quit") {
		if (!frontend->AskYesNo("Are you sure you want to quit?"))
			return true;
		// Signal the end of play before handing over, so that a frontend asking GameIsOngoing()
		// from within QuitGame() gets a truthful answer.
		gameHasBegun = false;
		frontend->QuitGame();
		return true;
	}
	if (cmd == "undo") {
		if (!UndoAvailable()) {
			OutputFiltered("Sorry, <c>undo</c> is not currently available.\n");
			return true;
		}
		// As with RESTART, `this` is gone once RestoreUndo() has put the previous state back.
		RestoreUndo();
		Game::Get()->OutputFiltered("Undone.\n");
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
