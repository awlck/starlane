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

#include "error.h"
#include "gamecontent/task.h"
#include "gamecontent/utility.h"

namespace Starlane {

template<typename K, typename V> V *SafeMapGet(const std::unordered_map<K, V *> &map, const K &key) {
	auto f = map.find(key);
	return f == map.end() ? nullptr : f->second;
}

// Look something up by key in one of the load-order tables (Game::objects and friends): the key
// names a slot, via an index table that lives in the immutable GameStatic, and the slot holds the
// thing. Returns nullptr for a key the game hasn't got.
template<typename T> T *IndexedGet(const std::unordered_map<std::string, size_t> &index,
                                   const std::vector<T *> &table, const std::string &key) {
	auto f = index.find(key);
	return f == index.end() ? nullptr : table[f->second];
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
	// The author's preferred display font/colors (<FontName>/<InputColour>/<OutputColour>), if the
	// game specifies any. The colors are std::nullopt (rather than some baked-in default) when the
	// element is absent, so a frontend can tell "the author didn't say" from "the author chose
	// black" and fall back to its own default only in the former case.
	std::string gameFontName;
	std::optional<uint32_t> gameInputColour;
	std::optional<uint32_t> gameOutputColour;
	bool showFirstLocation = true;
	bool showExits = true;
	// How many turns a single WAIT command lets pass.
	uint32_t waitTurns = 3;
	DescrRef gameIntro = 0;
	DescrRef userStatusBar = 0;
	Util::DirectionTable directionTable;

	// immutable content (only exists once)
	std::unordered_map<RestrRef, Restriction *> restrictions;
	std::unordered_map<std::string, Property *> properties;
	std::unordered_map<std::string, Task *> tasks;
	// Variable name (lowercased) -> that variable's slot in Game::variables.
	std::unordered_map<std::string, size_t> varNames;
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
	// Where to find a thing by key. The mutable state these index into (Game::objects and
	// friends) is a flat vector in load order rather than a hash map, for two reasons: the whole
	// of it is deep-copied into an undo snapshot once per turn, and a map's worth of nodes and
	// copied key strings was one of the more expensive parts of that; and load order is an order
	// the engine needs constantly anyway -- it is the order things are listed to the player in,
	// and the order ADRIFT ticks events in -- so the vector's own order is the useful one.
	//
	// Every one of these tables is filled during load and never added to or removed from
	// afterwards, which is what makes a thing's index a stable name for it.
	std::unordered_map<std::string, size_t> objectIndex;
	std::unordered_map<std::string, size_t> eventIndex;
	std::unordered_map<std::string, size_t> variableIndex;
	std::unordered_map<std::string, size_t> groupIndex;

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
	// Tear down the current game (and any undo history) and leave no current game behind. Used by
	// the top-level backstop when loading throws partway, so a half-built game can't linger.
	static void Discard();
	DescrRef CreateDescFromXML(const pugi::xml_node &descNode);
	DescrRef CreateDescFromText(const std::string &text);
	RestrRef CreateRestrictionsFromXML(const pugi::xml_node &restrNode);
	PlainTextRef StorePlainTextSnippet(const std::string &snip);
	PlainTextRef StorePlainTextSnippet(std::string_view snip);
	ExprRef CreateExpression(const std::string &expr);

	// ---- Reading the world ------------------------------------------------------------------
	// Everything below hands back a pointer you may not write through. To change something, ask
	// for it by the matching Mutable* accessor further down instead; the compiler will tell you
	// when you need one. That is the whole point: it is how "nothing changes the world without
	// the undo machinery being told" stops being a rule people have to remember.
	const Description *GetDescription(DescrRef d) const {
		// DescrRefs are handed out sequentially from 1 (see CreateDescFromXML), so the table is a
		// flat vector rather than a hash map: it is indexed on every description built, and it is
		// deep-copied once per turn for the undo snapshot. 0 means "no description" and is not a
		// valid subscript -- throw for it exactly as the map lookup this replaced did.
		if (d == 0 || d >= descriptions.size()) throw std::out_of_range("no such description");
		return descriptions[d];
	}
	const Event *GetEvent(const std::string &key) const { return IndexedGet(staticData->eventIndex, events, key); }
	const Group *GetGroup(const std::string &key) const { return IndexedGet(staticData->groupIndex, groups, key); }
	// Look up an object by key, returning nullptr if none exists. Use this only where a missing
	// key is a legitimate, expected answer -- existence/type probes (`AsLocation(...)`) and
	// explicit `if (TryGetObject(k))` guards.
	const GameObj *TryGetObject(const std::string &key) const { return IndexedGet(staticData->objectIndex, objects, key); }
	// Look up an object by key that the caller expects to exist. Throws MissingObjectException
	// (logging the key) rather than returning a dangling nullptr to be dereferenced -- either the
	// game file is malformed or a reference was evaluated while unset. Callers that cannot tolerate
	// this sit under a funnel-level try/catch (restriction/action evaluation) or the top-level
	// backstop in starlane-core.cpp.
	const GameObj *GetObject(const std::string &key) const {
		const GameObj *o = TryGetObject(key);
		if (!o) throw MissingObjectException(key);
		return o;
	}

	// ---- Changing the world -----------------------------------------------------------------
	// The only way to get a pointer you may write through. Today these do nothing but drop the
	// const; the undo machinery hangs off them next, at which point asking for one is also what
	// records the object's previous state.
	GameObj *MutableObject(const std::string &key) { return Unconst(TryGetObject(key)); }
	GameObj *MutableObject(size_t slot) { return Unconst(objects[slot]); }
	// The must-exist form, matching GetObject.
	GameObj *MutableObjectChecked(const std::string &key) { return Unconst(GetObject(key)); }
	Event *MutableEvent(const std::string &key) { return Unconst(GetEvent(key)); }
	Event *MutableEvent(size_t slot) { return Unconst(events[slot]); }
	Group *MutableGroup(const std::string &key) { return Unconst(GetGroup(key)); }
	Group *MutableGroup(size_t slot) { return Unconst(groups[slot]); }
	Variable *MutableVariable(const std::string &key) { return Unconst(GetVariable(key)); }
	Variable *MutableVariable(size_t slot) { return Unconst(variables[slot]); }
	Variable *MutableVarByName(const std::string &name) { return Unconst(GetVarByName(name)); }
	Description *MutableDescription(DescrRef d) { return Unconst(GetDescription(d)); }
	GameObj *MutablePlayerChar() { return Unconst(GetPlayerChar()); }
	Task *GetTask(const std::string &key) const { return SafeMapGet(staticData->tasks, key); }
	// Whether `childKey` is one of `parentKey`'s direct Specific children -- ADRIFT's task.Children,
	// which an Event/Walk control uses to ignore a re-trigger by a child of the task it just handled.
	bool TaskIsSpecificChildOf(const std::string &childKey, const std::string &parentKey) const;
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
	const Variable *GetVariable(const std::string &key) const { return IndexedGet(staticData->variableIndex, variables, key); }
	// varNames maps straight to the slot, so looking a variable up by the name a game writes it
	// under ("%Seabonus%") costs the same single lookup as looking it up by key.
	const Variable *GetVarByName(const std::string &name) const { const auto f = staticData->varNames.find(Util::ToLower(name)); return f == staticData->varNames.end() ? nullptr : variables[f->second]; }
	const UserFunction *GetUserFunction(const std::string &key) const { return SafeMapGet(staticData->userFunctions, key); }
	const UserFunction *GetUserFuncByName(const std::string &name) const { const auto f = staticData->userFuncNames.find(name); return f == staticData->userFuncNames.end() ? nullptr : staticData->userFunctions.at(f->second); }
	Expression *GetExpression(ExprRef ref) { return staticData->expressions.at(ref); }
	const char *GetPlainTextSnippet(PlainTextRef ref) const { return staticData->plainTextSnippets.at(ref); }

	// Whether the player is at the location with this key, or at a location belonging to the
	// group with this key. Used by subevents that only speak up in certain places -- the field
	// naming those is called "OnlyApplyAt" and holds either sort of key. A key naming neither a
	// location nor a group matches nowhere.
	bool PlayerIsInLocationOrGroup(const std::string &key) const;
	bool GroupExists(const std::string &key) const { return staticData->groupIndex.count(key) > 0; }
	bool ObjectExists(const std::string &key) const { return staticData->objectIndex.count(key) > 0; }
	bool PropExists(const std::string &key) const { return staticData->properties.find(key) != staticData->properties.end(); }
	bool VarOfNameExists(const std::string &name) const { return staticData->varNames.find(Util::ToLower(name)) != staticData->varNames.end(); }
	// Every object in the game, in the order they appear in the game file. That order is the one
	// callers want: it is how things are listed to the player, and how ADRIFT itself enumerates.
	const std::vector<const GameObj *> &GetAllObjects() const { return objects; }
	// The same for events -- the order they tick in is observable, since one event's subevent can
	// start or stop another, and it is the order ADRIFT ticks them in too.
	const std::vector<const Event *> &GetAllEvents() const { return events; }
	const GameObj *GetPlayerChar() const { return objects[staticData->objectIndex.at(playerKey)]; }
	// The player's slot, for the paths that need to change them rather than read them.
	size_t PlayerSlot() const { return staticData->objectIndex.at(playerKey); }
	// The player's key, for asking "is this the player?" without a lookup -- and without the
	// throw GetPlayerChar() would give for a question asked before the player has been picked.
	const std::string &GetPlayerKey() const { return playerKey; }
	// Make `newPlayerKey` the character the player is playing as, per `MoveCharacter ...
	// ToSwitchWith` when either side of the switch is the player. Neither character actually
	// moves; the old player's pronoun descriptors ("me", "myself", ...) move to the new one, as
	// in the original ADRIFT runner.
	void SwitchPlayerCharacter(const std::string &newPlayerKey);

	bool GetIsTaskCompleted(const std::string &key) const {
		return taskCompletedStorage[TaskStateIndex(key)] != 0;
	}
	void SetTaskCompleted(const std::string &key, bool val) {
		taskCompletedStorage[TaskStateIndex(key)] = val ? 1 : 0;
	}
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

	// The one place in the interpreter that drops const from a piece of game state. Everything
	// that changes the world goes through a Mutable* accessor above, and every one of those goes
	// through here -- so this is the single door the undo machinery gets to stand in.
	template<typename T> static T *Unconst(const T *p) { return const_cast<T *>(p); }

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
	// Identifies the newest saved undo state, or 0 when there is none. Used by the top-level
	// backstop (starlane-core.cpp) to tell whether a turn recorded a snapshot before it threw, and
	// hence whether to roll it back: the counter only ever goes up, so a value *greater* than the
	// one read before the turn means "a state recorded during this turn is still the newest one".
	// A plain count will not do -- SaveUndo pushes before trimming to kMaxUndoStates, so once the
	// history is full the depth is the same before and after and a failed turn was never rolled
	// back.
	static uint64_t TopUndoGeneration() { return undoStates.empty() ? 0 : undoStates.back()->undoGeneration; }
	// Number of saved undo states, for anything that genuinely wants the count.
	static size_t UndoDepth() { return undoStates.size(); }
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
	// Get status bar info
	bool GetStatusBar(StatusBar &statusBar);
	// Const, like OutputFiltered which calls it, even though building an override's replacement
	// text records that its segments were shown. That write goes through MutableDescription like
	// every other, which is what matters -- the undo machinery hangs off the accessor, not off
	// whether the Game happened to be const at the call.
	void ApplyOverrides(std::string &t) const;

	ReferralPerson GetCurrentReferralPerson() const {
		if (mostRecentlyMentioned.first.empty() || mostRecentlyMentioned.first == playerKey)
			return staticData->pcReferralPerson;
		return ReferralPerson::ThirdPerson;
	}
	ReferralPerson GetPCReferralPerson() const { return staticData->pcReferralPerson; }
	const std::pair<std::string, Pronoun> &GetMostRecentlyMentioned() const { return mostRecentlyMentioned; }
	void MentionCharacter(const std::string &key, Pronoun p) {
		// Suppressed while a Description::Build(false) frame (or anything nested within one) is
		// evaluating -- that pass is a throwaway measurement never shown to the player, so it must
		// not be able to make a *later*, real Build() print a pronoun instead of a name. See
		// mentionTrackingSuppressed.
		if (mentionTrackingSuppressed) return;
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
	// RAII guard used by Description::Build to suppress MentionCharacter's writes for the duration
	// of a commit=false ("measuring") build -- mirroring how that same commit flag already gates
	// Description::HandleSegmentShown. Nests correctly (a counter, not a bool) since a measuring
	// build can itself evaluate a nested Description::Build call. `active` is false for an ordinary
	// commit=true build, which should have no effect on the ambient suppression state.
	struct MentionTrackingSuppressGuard {
		Game *g;
		bool active;
		MentionTrackingSuppressGuard(Game *g, bool active) : g(g), active(active) {
			if (active) g->mentionTrackingSuppressed++;
		}
		~MentionTrackingSuppressGuard() { if (active) g->mentionTrackingSuppressed--; }
	};

	const std::string &GetTitle() const { return staticData->gameTitle; }
	const std::string &GetAuthor() const { return staticData->gameAuthor; }
	const std::string &GetLastUpdated() const { return staticData->gameLastUpdated; }
	uint32_t GetChecksum() const { return staticData->gameCrc32; }
	const std::string &GetFontName() const { return staticData->gameFontName; }
	std::optional<uint32_t> GetInputColour() const { return staticData->gameInputColour; }
	std::optional<uint32_t> GetOutputColour() const { return staticData->gameOutputColour; }
	const Util::DirectionTable &GetDirectionTable() const { return staticData->directionTable; }
	bool ShowExits() const { return staticData->showExits; }

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
	// currently captured in currentRefs (as named by refTokens, positionally), or -- for a
	// reference whose Specific constraint names more than one key -- by the full set currentRefLists
	// recorded for it (see SpecificTaskMatches's multi-key branch).
	bool SpecificTaskMatches(const Task *specific, const std::vector<std::string> &refTokens) const;
	// Run a matched General task to completion, applying any overriding/extending Specific
	// tasks per their OverrideType, and output whatever text results.
	struct ResponseBuffer;  // defined with the transient storage below
	void ExecuteMatchedTask(Task *general);
	// Emit every completion message a command collected, once, at end of command -- merging the
	// object/character references of runs whose (aggregated) message coincided so a multi-object
	// command renders one combined sentence. See RunTaskAndCapture and ADRIFT's "Aggregate output".
	void FlushResponseBuffer(ResponseBuffer &buffer);
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

	// Mutable game state (deep-copied for the undo state). Each of these is in load order, and a
	// thing's position in it is fixed for the life of the game -- see the index tables in
	// GameStatic, which are how a key gets turned into a position.
	std::vector<const GameObj *> objects;
	std::vector<const Event *> events;
	std::vector<const Variable *> variables;
	std::vector<const Group *> groups;
	// Indexed by DescrRef; slot 0 is the "no description" sentinel and stays null.
	std::vector<const Description *> descriptions{nullptr};
	// Stores the completed-ness of tasks to avoid needing to copy the entire tasks for saves.
	// Indexed by Task::StateIndex() rather than keyed by task key: this is copied wholesale once
	// per turn for the undo snapshot, and a map of one string key per task in the game made that
	// one of the more expensive parts of a snapshot. (uint8_t rather than bool so that the copy
	// is a plain memcpy instead of vector<bool>'s bit twiddling.)
	std::vector<uint8_t> taskCompletedStorage;
	// The slot in `taskCompletedStorage` belonging to the task with this key. Throws (as the map
	// lookup this replaced did) if no such task exists.
	size_t TaskStateIndex(const std::string &key) const;
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

	// One completion message collected during a command, awaiting the end-of-command flush that
	// prints it. Mirrors an entry in ADRIFT's per-command response table. See RunTaskAndCapture.
	struct AggregatedResponse {
		DescrRef descr;                                             // the message to render at flush time
		// The references in effect when this message was first recorded; the flush re-renders against
		// these (plus any merged overrides below).
		std::unordered_map<std::string, std::string> refSnapshot;
		// Reference name -> the distinct keys it took across the runs that collapsed into this one
		// message. Populated only for names whose value actually varied; at flush a name with more
		// than one key is bound to the pipe-joined list, which %objects%.Name / %TheObject[...]%
		// expand to "the ball and the box".
		std::unordered_map<std::string, std::vector<std::string>> mergedRefs;
	};
	// The completion messages a single player command has produced so far, keyed by their dedup
	// string (unevaluated text for an aggregating task, evaluated text otherwise), in insertion
	// order. Opened for the duration of ExecuteMatchedTask; nested Execute actions (SetTasks FOR
	// loops included) record into the same buffer, so their messages aggregate too.
	struct ResponseBuffer {
		std::vector<std::string> order;
		std::unordered_map<std::string, AggregatedResponse> byKey;
	};
	// Non-null while a player command is buffering its completion messages; null on the out-of-command
	// paths (events, character walks, triggered tasks), which keep emitting immediately with the
	// simple turn-wide dedup in completionMessagesThisTurn. Transient: never saved or undone.
	ResponseBuffer *activeResponseBuffer = nullptr;

	// Characters named via character.Name/%CharacterName% so far this turn, and the pronoun they
	// were last shown as -- consulted so a character is only pronominalised ("he"/"she"/"it") once
	// the player has actually seen them named this turn, and so that an Object mention right after
	// a Subject one upgrades to Reflective ("he saw himself" rather than "he saw him"). Reset at
	// the start of every ProcessInput, like turnHasOutput below: transient, never saved or undone.
	std::unordered_map<std::string, Pronoun> charactersMentionedThisTurn;
	// >0 while a Description::Build(false) frame -- or anything nested within one -- is evaluating.
	// See MentionCharacter/MentionTrackingSuppressGuard.
	int mentionTrackingSuppressed = 0;

	// For each object/character %ref% captured, the raw text the player typed for it and the full
	// list of object keys that text could refer to. currentRefs keeps only the first of those (the
	// provisional resolution); this keeps the rest, so that once a task is chosen we can tell an
	// ambiguous reference from an unambiguous one and ask the player to clarify. Same lifecycle as
	// currentRefs: transient, cleared and repopulated per match, never saved or undone.
	// (RefMatchInfo is defined up with the parser internals, where its first user is.)
	std::unordered_map<std::string, RefMatchInfo> currentRefMatches;

	// For a *plural* reference ("%objects%") that named several things at once ("the plates and the
	// ball"), the per-item match info: one RefMatchInfo per named piece, in the same order as that
	// reference's entry in currentRefLists. Any single piece can itself be ambiguous ("the ball" with
	// two balls present) -- which currentRefMatches, one entry per whole reference, has no room to
	// express -- so those questions are driven from here instead. Same transient lifecycle as
	// currentRefMatches/currentRefLists. Only plural references (pieces > 1) appear here; a
	// single-piece reference still disambiguates through currentRefMatches as a singular one does.
	std::unordered_map<std::string, std::vector<RefMatchInfo>> currentRefItemMatches;

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
		// = currentRefItemMatches at the time: per-item candidates for each plural reference, so a
		// question about one item of a plural reference can be resolved and its refLists slot updated.
		std::unordered_map<std::string, std::vector<RefMatchInfo>> itemMatches;
	};
	std::optional<PendingDisambig> pendingDisambig;

	// The first reference slot still matching several objects, in refTokens order: a whole singular
	// reference (from refMatches) or one item of a plural reference (from itemMatches). Returns the
	// RefMatchInfo to ask about (nullptr if none); outItemIdx is the plural item index, or -1 for a
	// singular reference. Drives both BeginDisambiguationIfNeeded and ResolveDisambiguation.
	static RefMatchInfo *FirstAmbiguousSlot(
		const std::vector<std::string> &refTokens,
		std::unordered_map<std::string, RefMatchInfo> &refMatches,
		std::unordered_map<std::string, std::vector<RefMatchInfo>> &itemMatches,
		std::string &outToken, int &outItemIdx);
	// Apply a disambiguated plural item choice to a held command: update that item's slot in
	// pd.refLists and, for item 0, the reference's provisional single binding.
	static void SetPluralItemChoice(PendingDisambig &pd, const std::string &token,
	                                int itemIdx, const std::string &chosenKey);

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
	// Task completion messages already shown this turn, exactly as displayed. Used by the
	// out-of-command emit path (events, walks, triggered tasks) to suppress a message that already
	// appeared this turn, and topped up by FlushResponseBuffer so those paths still dedup against a
	// command's own messages. Within a command, aggregation is handled by activeResponseBuffer
	// instead. Same lifecycle as turnHasOutput above: reset every ProcessInput, never saved/undone.
	mutable std::unordered_set<std::string> completionMessagesThisTurn;

	// (No descriptionsSoFar: `descriptions` is a dense vector, so its size is the count.)
	size_t restrictionsSoFar = 0;
	ptrdiff_t textSnippetsSoFar = 0;
	ptrdiff_t expressionsSoFar = 0;

	// The Game instance holding the current state of the game, for the benefit of any
	// functions that might need it (restrictions, descriptions, action processing)
	static Game *theGame;
	// The list of former game states maintained for use with the UNDO command.
	static std::deque<Game *> undoStates;
	// Which SaveUndo recorded this state; 0 for a state that is not on the undo list. See
	// TopUndoGeneration.
	uint64_t undoGeneration = 0;
	// Handed out by SaveUndo, never reused.
	static uint64_t undoGenerationCounter;
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