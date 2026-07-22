#pragma once

#ifndef SLC_GAME_H
#define SLC_GAME_H

#include "slc_private.h"

#include <deque>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <string_view>
#include <vector>

#include "gamecontent/task.h"
#include "gamecontent/utility.h"

namespace Starlane {

template<typename K, typename V> V *SafeMapGet(std::unordered_map<K, V *> map, const K &key) {
	auto f = map.find(key);
	return f == map.end() ? nullptr : f->second;
}

enum class ReferralPerson {
	FirstPerson,
	SecondPerson,
	ThirdPerson
};

enum class ExecutionPolicy {
	/* Execute the highest priority task that matches command input, whether it passes or not. */
	HighestPrio,
	/* Execute the highest priority task matching command input that passes restrictions. If
	 * none are found, execute the highest priority task matching command input that fails restrictions. */
	HighestPrioPassing
};

class GameStatic {
	std::string gameTitle;
	std::string gameAuthor;
	std::string gameAdriftVersion;
	std::string gameLastUpdated;
	uint32_t gameCrc32;
	std::string gameStatusLine;
	bool showFirstLocation = true;
	bool showExits = true;
	// How many turns a single WAIT command lets pass.
	uint32_t waitTurns = 3;
	DescrRef gameIntro = 0;

	// immutable content (only exists once)
	std::unordered_map<RestrRef, Restriction *> restrictions;
	std::unordered_map<std::string, Property *> properties;
	std::unordered_map<std::string, Task *> tasks;
	std::unordered_map<std::string, std::string> varNames;
	std::unordered_map<ExprRef, Expression *> expressions;
	std::unordered_map<std::string, UserFunction *> userFunctions;
	std::unordered_map<std::string, std::string> userFuncNames;
	std::unordered_map<std::string, Synonym *> synonyms;
	std::unordered_map<std::string, TextOverride *> textOverrides;
	// Snippets of "Plain text", simple strings that do not contain any expressions
	// and can be output as-is. Maintained like this to reduce the amount of text
	// that is unnecessarily duplicated when copying descriptions for undo/save.
	std::unordered_map<PlainTextRef, const char *> plainTextSnippets;
	// The grammatical person by which to refer to the player character
	ReferralPerson pcReferralPerson = ReferralPerson::SecondPerson;
	ExecutionPolicy executionPolicy = ExecutionPolicy::HighestPrio;
	// The mapping of file path -> Blorb resource ID, if applicable.
	std::unordered_map<std::string, uint32_t> blorbResMap;
	// The keys of all game objects in the order they appear in the game file, since
	// listing objects in a stable order requires it (the objects map is unordered).
	std::vector<std::string> objectLoadOrder;
	// The same for events, and for much the same reason: they are held in an unordered map, but
	// the order they tick in is observable -- one event's subevent can run a task that starts or
	// stops another -- so it has to be the order the game file lists them in, which is the order
	// ADRIFT ticks them in too.
	std::vector<std::string> eventLoadOrder;

	// Tasks in priority order
	std::set<Task *, TaskPrioLess> prioOrderedTasks;
	// Specific tasks that override a given General task (by that General task's key), in
	// priority order. Populated once at load time from each Specific task's `overridesTask`.
	std::unordered_map<std::string, std::vector<Task *>> specificChildren;
	// System tasks that run when the player arrives somewhere, indexed by the location's key and
	// held in priority order. A System task has no command to match on, so this and the "run as
	// the game starts" flag are the only ways one runs without another task naming it outright.
	std::unordered_map<std::string, std::vector<Task *>> systemTasksByLocation;
	// ...and the ones that run once, as the game starts, in priority order.
	std::vector<Task *> runImmediatelyTasks;

	friend class Game;
public:
	~GameStatic();
};

class Game {
public:
	// Gets the current game instance
	static inline Game *Get() { return theGame; }

	static Game *LoadFromXML(const std::string &gameTxt, uint32_t crc32);
	DescrRef CreateDescFromXML(const pugi::xml_node &descNode);
	DescrRef CreateDescFromText(const std::string &text);
	RestrRef CreateRestrictionsFromXML(const pugi::xml_node &restrNode);
	PlainTextRef StorePlainTextSnippet(const std::string &snip);
	PlainTextRef StorePlainTextSnippet(std::string_view snip);
	ExprRef CreateExpression(const std::string &expr);

	Description *GetDescription(DescrRef d) const { return descriptions.at(d); }
	Event *GetEvent(const std::string &key) { return SafeMapGet(events, key); }
	Group *GetGroup(const std::string &key) { return SafeMapGet(groups, key); }
	GameObj *GetObject(const std::string &key) { return SafeMapGet(objects, key); }
	Task *GetTask(const std::string &key) const { return SafeMapGet(staticData->tasks, key); }
	// Directly run a task by key (used by the "Execute <task>" task action), independent of
	// player command matching: check its restrictions, run its actions if they pass, mark it
	// completed, and output its completion (or restriction failure) message. Does nothing if
	// no task with that key exists.
	// `args`, if any, are the values the calling task supplies for the called task's own %ref%s
	// (`Execute MoveOutObject (%Player%.Parent)`), bound positionally for the duration of the
	// call. With none, the caller's own references stay visible, as they must for a task that
	// simply hands off to another with the same references in play.
	void ExecuteTaskByKey(const std::string &key, const std::vector<std::string> &args = {});
	// Note that the player has arrived at the location with this key, so that any System task
	// triggered by arriving there is lined up to run. Called by Character::MoveTo; does nothing
	// for anyone but the player, or for a move that stays in the same location.
	void NotePlayerArrived(const std::string &locationKey);
	// Run whatever the moves above lined up, in the order they were lined up. Called once the
	// player's command has been dealt with and before the world takes its turn -- not from
	// inside the move itself, which would run a task in the middle of another task's actions.
	void RunTriggeredTasks();
	const Property *GetPropMeta(const std::string &key) const { return SafeMapGet(staticData->properties, key); }
	const Restriction *GetRestriction(RestrRef key) const { return staticData->restrictions.at(key); }
	Variable *GetVariable(const std::string &key) { return SafeMapGet(variables, key); }
	Variable *GetVarByName(const std::string &name) { const auto f = staticData->varNames.find(Util::ToLower(name)); return f == staticData->varNames.end() ? nullptr : variables.at(f->second); }
	const UserFunction *GetUserFunction(const std::string &key) const { return SafeMapGet(staticData->userFunctions, key); }
	const UserFunction *GetUserFuncByName(const std::string &name) const { const auto f = staticData->userFuncNames.find(name); return f == staticData->userFuncNames.end() ? nullptr : staticData->userFunctions.at(f->second); }
	Expression *GetExpression(ExprRef ref) { return staticData->expressions.at(ref); }
	const char *GetPlainTextSnippet(PlainTextRef ref) const { return staticData->plainTextSnippets.at(ref); }

	// Whether the player is at the location with this key, or at a location belonging to the
	// group with this key. Used by subevents that only speak up in certain places -- the field
	// naming those is called "OnlyApplyAt" and holds either sort of key. A key naming neither a
	// location nor a group matches nowhere.
	bool PlayerIsInLocationOrGroup(const std::string &key) const;
	bool GroupExists(const std::string &key) const { return groups.find(key) != groups.end(); }
	bool ObjectExists(const std::string &key) const { return objects.find(key) != objects.end(); }
	bool PropExists(const std::string &key) const { return staticData->properties.find(key) != staticData->properties.end(); }
	bool VarOfNameExists(const std::string &name) const { return staticData->varNames.find(Util::ToLower(name)) != staticData->varNames.end(); }
	const std::unordered_map<std::string, GameObj *> &GetAllObjects() const { return objects; }
	// All object keys, in the order the objects appear in the game file.
	const std::vector<std::string> &GetObjectLoadOrder() const { return staticData->objectLoadOrder; }
	GameObj *GetPlayerChar() const { return objects.at(playerKey); }
	// The player's key, for asking "is this the player?" without a map lookup -- and without the
	// throw GetPlayerChar() would give for a question asked before the player has been picked.
	const std::string &GetPlayerKey() const { return playerKey; }

	bool GetIsTaskCompleted(const std::string &key) const { return taskCompletedStorage.at(key); }
	void SetTaskCompleted(const std::string &key, bool val) { taskCompletedStorage[key] = val; }
	// The key of the location the player is currently in. Out of line because it needs
	// GameObj to be complete.
	const std::string &GetPlayerLocationKey() const;
	// Get an object referred to by the input. Reference names are matched without regard to
	// case, here as everywhere -- hence the canonicalized lookup (see Util::CanonicalizeRefName).
	const std::string &GetReference(const std::string &rk) {
		const std::string canon = Util::CanonicalizeRefName(rk);
		if (canon == "%player%" || canon == "player") return playerKey;
		// Not a captured reference but a pseudo-key the standard library uses to talk about
		// wherever the player happens to be (e.g. "PlayerLocation MustNot BeInGroup DarkLocations").
		if (canon == "playerlocation") return GetPlayerLocationKey();
		// This will insert a new element into the map if there is no such entry.
		// Not ideal, but there can only ever be twenty or so references, so I think it's OK.
		return currentRefs[canon];
	}
	// Determine if this reference holds anything.
	bool RefExists(const std::string &rk) const {
		const std::string canon = Util::CanonicalizeRefName(rk);
		if (canon == "%player%") return true;
		if (canon == "playerlocation") return !GetPlayerLocationKey().empty();
		return currentRefs.count(canon) > 0;
	}

	// Some references are not part of the game, but are used internally to pass some extra
	// context from one part of the engine to another.
	void SetInternalReference(const std::string &ref, const std::string &val) {
		currentRefs[Util::CanonicalizeRefName("<" + ref + ">")] = val;
	}
	// ...and those will need to be frequently cleared as well
	void ClearInternalReference(const std::string &ref) {
		currentRefs[Util::CanonicalizeRefName("<" + ref + ">")] = "";
	}

	// Save the current game state to the undo list, discarding the oldest state(s) if that
	// would take the list over `kMaxUndoStates`.
	void SaveUndo();
	// If any undo states are available, discard the current state and go back one step.
	bool RestoreUndo();
	// Discard the oldest saved game state.
	// Does nothing if there currently aren't any undo states.
	static void DiscardUndo();
	// Is there at least one undo state available?
	bool UndoAvailable() const { return !undoStates.empty(); }
	// Restart the game.
	void Restart();

	// This function must be called to start the game. It will start relevant events and
	// output the initial batch of text.
	void Begin();
	// This function should be called once per second to advance real-time-based events.
	void Tick();
	// Advance the world by one turn: every turn-based event gets a tick and the turn counter
	// moves on. Called once at the end of any command that changes the game world, once per
	// waited turn by WAIT, and repeatedly by a task's "skip N turns" action -- which is why it
	// has to be reachable from Task::Action, via Game::Get(), as ExecuteTaskByKey is.
	void TurnTick();
	uint32_t GetTurnCount() const { return turnCount; }
	// Whether we are presently inside RunEventTick. An event asked to start or stop by a task
	// consults this to decide whether to do so there and then or wait for its own next tick.
	bool AreEventsRunning() const { return eventsRunning; }
	// Save the game. Called by the action-processing machinery when the player types SAVE.
	bool Save();
	// Restore a saved game.
	bool Restore();

	ReferralPerson GetCurrentReferralPerson() const {
		if (mostRecentlyMentioned.first.empty() || mostRecentlyMentioned.first == playerKey)
			return staticData->pcReferralPerson;
		return ReferralPerson::ThirdPerson;
	}
	ReferralPerson GetPCReferralPerson() const { return staticData->pcReferralPerson; }
	const std::pair<std::string, Pronoun> &GetMostRecentlyMentioned() const { return mostRecentlyMentioned; }
	void MentionCharacter(const std::string &key, Pronoun p) {
		mostRecentlyMentioned = {key, p};
		charactersMentionedThisTurn[key] = p;
	}
	// The pronoun `key` was last shown as, if character.Name/%CharacterName% has already displayed
	// them at some point this turn -- nullopt if they haven't been (yet).
	std::optional<Pronoun> GetPronounMentionedThisTurn(const std::string &key) const {
		auto it = charactersMentionedThisTurn.find(key);
		if (it == charactersMentionedThisTurn.end()) return std::nullopt;
		return it->second;
	}

	const std::string &GetTitle() const { return staticData->gameTitle; }
	const std::string &GetAuthor() const { return staticData->gameAuthor; }
	const std::string &GetLastUpdated() const { return staticData->gameLastUpdated; }
	uint32_t GetChecksum() const { return staticData->gameCrc32; }

	bool IsGameOngoing() const { return gameHasBegun; }
	// Whether the frontend should keep reading input at all. True until the player actually
	// quits (or confirms QUIT after the game has ended) -- unlike IsGameOngoing, this stays true
	// across EndGame, since the player can still answer the final question.
	bool IsSessionActive() const { return sessionActive; }

	// How a game can stop. A task's "end the game" action says which; the player is told, and from
	// then on ProcessInput accepts only RESTART, RESTORE, QUIT, or UNDO (see
	// AttemptMatchEndOfGameCommand) until one of those puts the game running again or ends the
	// session.
	enum class Ending {
		Win,
		Lose,
		Neutral
	};
	void EndGame(Ending how);
	uint32_t GetBlorbResource(const std::string &path) const {
		auto f = staticData->blorbResMap.find(path);
		if (f == staticData->blorbResMap.cend()) return -1;
		return f->second;
	}

	// Submit player input for processing
	void ProcessInput(const std::string &s);

	// Send text to the frontend, with any text overrides the game defines applied to it first.
	// Public because an event's subevent displays its message through here.
	void OutputFiltered(std::string s) const;

private:
	Game() = default;
	Game(const Game &);  // copy constructor -- for undo state saving
	~Game();

	void CreateObjFromXML(const pugi::xml_node &objNode);
	void CreatePropertyFromXML(const pugi::xml_node &propNode);
	Task *CreateTaskFromXML(const pugi::xml_node &taskNode);
	void CreateEventFromXML(const pugi::xml_node &evtNode);
	void CreateVariableFromXML(const pugi::xml_node &varNode);
	void CreateGroupFromXML(const pugi::xml_node &grpNode);
	void CreateFunctionFromXML(const pugi::xml_node &funcNode);
	void CreateSynonymFromXML(const pugi::xml_node &synoNode);
	void CreateTextOverrideFromXML(const pugi::xml_node &toNode);

    void StartupSanityCheck() const;

	bool ContinueRestore(const Save::AstNode *node);
	bool RollbackRestore();

	// Command parser related internals
	std::string ApplySynonyms(std::string s);
	// Find the general task (if any) whose command matches currentCommand, honoring
	// staticData->executionPolicy: under HighestPrio, the first (in priority order) task whose
	// command syntax matches wins outright, whether or not it goes on to pass restrictions;
	// under HighestPrioPassing, scanning continues past non-passing matches in search of one
	// that passes, falling back to the first non-passing match if none ever does.
	// On return, currentRefs and currentMatchedRefTokens hold the references captured from the
	// matched command (needed to test that task's Specific children for applicability).
	Task *FindMatchingTask();
	// FindMatchingTask found nothing at all: as ADRIFT's NotUnderstood does, check whether
	// currentCommand is a single bare verb ("launch") that some task's command pattern would
	// accept given an object/character/direction to go with it, and if so print a targeted
	// "Launch what?"/"who?"/"where?" instead of the generic rejection. Returns whether it did.
	bool PromptForIncompleteVerb();
	// FindMatchingTask (and PromptForIncompleteVerb) found nothing at all: as ADRIFT's
	// NotUnderstood does, check whether currentCommand names a thing the player can currently
	// see, and if so print "I don't understand what you want to do with <thing>." instead of the
	// generic rejection. Returns whether it did.
	bool DescribeUnmatchedThing();
	// Resolve a single reference's raw matched text (e.g. "the sword") to the keys of all
	// currently known game objects of the given family ("object"/"character"/etc.) that it
	// could refer to. Matches in the narrowest non-empty scope win: objects currently
	// visible to the player beat objects merely seen before, which beat everything else.
	std::vector<std::string> MatchListForReference(const std::string &from, const std::string &refFamily) const;
	// Populate currentRefs from a command match, given the task's reference names for that
	// particular command (e.g. "%direction%", "%object1%") and the corresponding capture groups.
	// Returns false if some reference could not be resolved to anything (e.g. an %object%
	// referring to an object that doesn't exist), meaning the task cannot apply after all.
	bool CaptureReferences(const std::vector<std::string> &refSpecs, const std::smatch &matches);
	// Replace whole-word "it"/"them"/"him"/"her" in a line of player input with whatever they
	// currently stand for (per pronounItText et al.), so "eat it" parses exactly as "eat the
	// bar" would. A pronoun with no known antecedent yet is left as-is, which simply fails to
	// match anything -- the same outcome as if the player had typed a nonsense noun.
	std::string SubstitutePronouns(std::string s) const;
	// After a command's references are fully resolved (past any disambiguation), note whichever
	// object(s)/character(s) it named as the new antecedent for "it"/"them"/"him"/"her" in
	// whatever the player types next. Reads currentMatchedRefTokens/currentRefs/currentRefLists,
	// so it must run after those are settled -- see ExecuteMatchedTask, its only caller.
	void UpdatePronounAntecedents();
	// Bind one %ref% (and its equivalent spellings) to a resolved key, in the given table.
	// Also used to apply a disambiguation answer, which resolves into a held table rather than
	// into currentRefs.
	static void BindReference(std::unordered_map<std::string, std::string> &refs,
	                          const std::string &ref, const std::string &value);
	// For each object/character %ref% captured, the raw text the player typed for it and the full
	// list of object keys that text could refer to (see currentRefMatches / pendingDisambig).
	struct RefMatchInfo {
		std::string raw;
		std::vector<std::string> candidates;
	};
	// Having chosen a task, check whether any of its object/character references matched more than
	// one thing (per currentRefMatches). If so, stash a PendingDisambig, ask the player about the
	// first such reference, and return true -- the command does not run and the world does not
	// advance until they answer. Returns false (and does nothing) when every reference is unambiguous.
	bool BeginDisambiguationIfNeeded(Task *chosen);
	// Route a line of input that answers a pending disambiguation question. Narrows the current
	// ambiguous reference by the answer; on full resolution it runs the held command, otherwise it
	// asks about the next ambiguity, re-asks, or -- when the answer names no candidate but is itself
	// a command the game understands -- abandons the disambiguation and runs that command instead.
	// Caution: like ProcessInput, this can delete `this` (a fallen-through UNDO/RESTART).
	void ResolveDisambiguation(const std::string &answer);
	// Emit "Which <word>? The red ball or the green ball." for one ambiguous reference.
	void DisplayAmbiguityQuestion(const RefMatchInfo &info);
	// The subset of `candidates` every word of `answer` matches (per GameObj::MatchesNameWord, with
	// a literal "the" always accepted) -- the player's clarifying answer applied to the candidates.
	std::vector<std::string> NarrowByAnswer(const std::vector<std::string> &candidates, const std::string &answer);
	// The Specific tasks (if any) that override the General task with this key, in priority order.
	const std::vector<Task *> &GetSpecificChildren(const std::string &generalKey) const;
	// Whether a Specific task's per-reference constraints are satisfied by the references
	// currently captured in currentRefs (as named by refTokens, positionally).
	bool SpecificTaskMatches(const Task *specific, const std::vector<std::string> &refTokens) const;
	// Run a matched General task to completion, applying any overriding/extending Specific
	// tasks per their OverrideType, and output whatever text results.
	void ExecuteMatchedTask(Task *general);
	// Run `general` together with whichever of its Specific children currently apply, per their
	// OverrideType. Assumes `general`'s own restrictions have already passed. `refTokens` names
	// the references a child's positional constraints are checked against (see
	// SpecificTaskMatches).
	void RunTaskWithSpecifics(Task *general, const std::vector<std::string> &refTokens);
	// Run `task`'s actions and/or output its completion message, in whichever order that
	// task's own MessagePlacement calls for. Assumes restrictions already passed. Output
	// happens immediately (not buffered) so it interleaves correctly with any nested task
	// executions triggered by this task's own actions (e.g. a chained "Execute" action).
	// `showText`/`runActions` let a Specific task's OverrideType selectively suppress either
	// half of its General parent's own execution.
	// Returns whether it actually printed anything, which decides whether lower-priority tasks
	// matching the same command still get a look in.
	bool RunTaskAndCapture(Task *task, bool showText = true, bool runActions = true);
	// Give every event of the matching kind (real-time or turn-based) one tick. Both TurnTick
	// and Tick come through here; `realTime` is what tells the two populations apart.
	// (A bool rather than an Event::TimeType because game.h only forward-declares Event.)
	void RunEventTick(bool realTime);
	// Try to read currentCommand as one of the commands that address the interpreter rather than
	// the game world (SAVE, QUIT, UNDO, ...), running it if so and returning whether it matched.
	// Only consulted once no task has matched, so that a game remains free to define a task whose
	// command shadows any of these.
	// Caution: RESTART and UNDO replace the current Game instance wholesale, so this may well
	// `delete this` -- the caller must not touch the instance afterwards.
	bool AttemptMatchSystemCommand();
	// The subset of AttemptMatchSystemCommand offered once the game has ended (see EndGame):
	// RESTART, RESTORE, QUIT, or UNDO. Split out so that ProcessInput can restrict itself to
	// exactly these once gameHasBegun is false, without also matching SAVE or WAIT, which have
	// nothing left to act on. Same caution as AttemptMatchSystemCommand: may `delete this`.
	bool AttemptMatchEndOfGameCommand();

	// mutable game state (objects copied for undo state)
	std::unordered_map<std::string, GameObj *> objects;
	std::unordered_map<std::string, Event *> events;
	std::unordered_map<std::string, Variable *> variables;
	std::unordered_map<std::string, Group *> groups;
	std::unordered_map<DescrRef, Description *> descriptions;
	// stores the completed-ness of tasks to avoid needing to copy the entire tasks for saves.
	std::unordered_map<std::string, bool> taskCompletedStorage;
	// the current player character
	std::string playerKey;
	// most recently mentioned character and pronoun
	std::pair<std::string, Pronoun> mostRecentlyMentioned;
	// The display text ("the bar", "the guard") currently standing in for "it"/"them"/"him"/"her"
	// in the player's next command -- see SubstitutePronouns/UpdatePronounAntecedents. Empty means
	// no antecedent has been established yet. Session state, like mostRecentlyMentioned above: not
	// written to save files, but carried across UNDO by the copy constructor below.
	std::string pronounItText, pronounThemText, pronounHimText, pronounHerText;
	// Turns elapsed, as reported by the `Turns` expression function. Counted once per TurnTick,
	// so a WAIT that lets three turns pass counts three of them. ADRIFT instead bumps its own
	// counter once per typed command, from the frontend, which has a three-turn WAIT count as
	// one -- the same splitting of "what the player typed" from "what the world did" that had
	// UNDO skipping whole commands.
	uint32_t turnCount = 0;

	// static data lives here for performance and memory usage reasons:
	const GameStatic *staticData;

	// transient storage -- only relevant while evaluating commands.
	// Never needs to be retained for UNDO/SAVE.
	std::unordered_map<std::string, std::string> currentRefs;
	std::string currentCommand;
	// The %ref% tokens (e.g. "%object1%", "%direction%"), in order, captured into currentRefs
	// by the most recent successful match in FindMatchingTask -- needed to test the matched
	// task's Specific children against currentRefs positionally.
	std::vector<std::string> currentMatchedRefTokens;
	// Plural references ("%objects%") that the player's command bound to more than one thing, as
	// (reference name, resolved keys). ExecuteMatchedTask runs the task once per combination.
	std::vector<std::pair<std::string, std::vector<std::string>>> currentRefLists;

	// Characters named via character.Name/%CharacterName% so far this turn, and the pronoun they
	// were last shown as -- consulted so a character is only pronominalised ("he"/"she"/"it") once
	// the player has actually seen them named this turn, and so that an Object mention right after
	// a Subject one upgrades to Reflective ("he saw himself" rather than "he saw him"). Reset at
	// the start of every ProcessInput, like turnHasOutput below: transient, never saved or undone.
	std::unordered_map<std::string, Pronoun> charactersMentionedThisTurn;

	// For each object/character %ref% captured, the raw text the player typed for it and the full
	// list of object keys that text could refer to. currentRefs keeps only the first of those (the
	// provisional resolution); this keeps the rest, so that once a task is chosen we can tell an
	// ambiguous reference from an unambiguous one and ask the player to clarify. Same lifecycle as
	// currentRefs: transient, cleared and repopulated per match, never saved or undone.
	// (RefMatchInfo is defined up with the parser internals, where its first user is.)
	std::unordered_map<std::string, RefMatchInfo> currentRefMatches;

	// A command that matched a task but left one or more of its references ambiguous, held while we
	// ask the player which object they meant. Their next line of input is routed to the resolver
	// (see ProcessInput) rather than parsed as a fresh command. Transient by nature -- asking is
	// not a turn, so nothing here is ever saved or undone.
	struct PendingDisambig {
		Task *task;                                             // the chosen general task (stable staticData pointer)
		std::vector<std::string> refTokens;                     // = currentMatchedRefTokens at the time
		std::unordered_map<std::string, std::string> refs;      // resolutions so far (provisional for the ambiguous ones)
		std::unordered_map<std::string, RefMatchInfo> refMatches; // raw text + remaining candidates, keyed like currentRefMatches
		// = currentRefLists at the time. Held because answering can run FindMatchingTask (when the
		// answer turns out to be a command of its own), which repopulates the live one.
		std::vector<std::pair<std::string, std::vector<std::string>>> refLists;
	};
	std::optional<PendingDisambig> pendingDisambig;

	// used at load-time to prevent duplicating expressions too much
	std::unordered_map<std::string, ExprRef> knownExprs;

	// System tasks lined up by the player arriving somewhere, in arrival order. Transient: it is
	// filled and emptied within a single command, so it never needs to survive UNDO or SAVE.
	std::deque<std::string> triggeredTasks;
	// Whether a tick is in progress. Transient: it only ever means anything within one call to
	// RunEventTick, so it never needs to survive UNDO or SAVE.
	bool eventsRunning = false;
	// Whether we are part-way through handling a player command. A real-time tick must not cut
	// in on one: a frontend prompting the player (a modal question, a file dialog) spins its own
	// event loop while ProcessInput is still on the stack, so the timer can fire in the middle of
	// a half-finished command. Static because ProcessInput can destroy the Game outright, on
	// RESTART or UNDO, and the flag has to outlive that.
	static bool inputInFlight;

	bool gameHasBegun = false;
	// Whether the session is still worth reading input for at all. Only QUIT (see
	// AttemptMatchEndOfGameCommand) ever clears this; EndGame leaves it set, since the final
	// question is itself something the player answers through ProcessInput.
	bool sessionActive = true;

	// Output separation within a turn: whether anything has been printed yet, and whether it ended
	// a line. See OutputFiltered -- two messages in the same turn are separated by two spaces
	// unless the earlier one already broke the line. Mutable because OutputFiltered is const.
	mutable bool turnHasOutput = false;
	mutable bool endsWithNewline = false;
	// Task completion messages already shown this turn, exactly as displayed. A task whose message
	// duplicates one already shown this turn (e.g. the same task run repeatedly by a SetTasks FOR
	// loop) is not printed again, approximating ADRIFT's "Aggregate output" task property -- see
	// RunTaskAndCapture. Same lifecycle as turnHasOutput above: reset every ProcessInput, never
	// saved or undone.
	mutable std::unordered_set<std::string> completionMessagesThisTurn;

	size_t descriptionsSoFar = 0;
	size_t restrictionsSoFar = 0;
	ptrdiff_t textSnippetsSoFar = 0;
	ptrdiff_t expressionsSoFar = 0;

	// The Game instance holding the current state of the game, for the benefit of any
	// functions that might need it (restrictions, descriptions, action processing)
	static Game *theGame;
	// The list of former game states maintained for use with the UNDO command.
	static std::deque<Game *> undoStates;
	// How many of them to keep around: each one is a full copy of the mutable game state, so
	// they are not cheap. (ADRIFT 5 settles on 100 as well.)
	static constexpr size_t kMaxUndoStates = 100;
	// How deeply turn ticks may nest before we refuse to go further. Only a task's "skip N turns"
	// action can nest them at all, and only a game that has one run from an event it also drives
	// can nest them without bound.
	static constexpr int kMaxTickDepth = 32;
	// How many arrival-triggered System tasks may run off a single command before we assume the
	// game has tied a knot -- one of them moving the player somewhere that triggers another.
	static constexpr size_t kMaxTriggeredTasks = 64;
	// The initial state right as the game starts. Maintained for the benefit of the
	// `restart` command.
	static Game *startupState;

	static ReferralPerson ParseReferralPerson(const char *p);
};

}

#endif  // !SLC_GAME_H