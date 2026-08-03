#include "game.h"

#include <algorithm>
#include <regex>

#include "starlane-core.h"
#include "debuglog.h"
#include "gamecontent/character.h"
#include "gamecontent/event.h"
#include "expression.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
#include "gamecontent/location.h"
#include "gamecontent/task.h"
#include "gamecontent/property.h"
#include "gamecontent/description.h"
#include "gamecontent/restriction.h"
#include "gamecontent/synonym.h"
#include "gamecontent/textoverride.h"
#include "gamecontent/userfunc.h"
#include "gamecontent/variable.h"
#include "savefiles/parser.h"
#include <cstring>

#include "savefiles/writer.h"
#include "valueparsers.h"

namespace Starlane {

namespace {
// Drop the marker Description::Build puts at the seam between description parts. It exists only to
// keep auto-capitalisation from treating an appended part as a new sentence; it is not text, and no
// game text reaches a frontend with it still in.
void StripSeamMarkers(std::string &s) {
	for (size_t pos = 0; (pos = s.find("<>", pos)) != std::string::npos; )
		s.erase(pos, 2);
}

// Raise the lowercase letter starting each sentence, so that games can write their messages
// without worrying where the text will end up ("%CharacterName% get[//s] out of
// %TheObject[%object%]%." is meant to read "You get out of the cell air duct.", and
// %CharacterName% has no way of knowing it came first).
//
// A transcription of ADRIFT's own rule (Global.vb, in ReplaceALRs), quirks included, since the
// point is to produce the text the ADRIFT runner produces:
//   ^(?<cap>[a-z])|\n(?<cap>[a-z])|[a-z][\.\!\?] ( )?(?<cap>[a-z])
// Note what it does *not* do. It only ever touches a letter, so a message opening with a tag
// ("<c>(out of a cell air duct)</c>") is left exactly as the author wrote it. And the
// sentence-boundary case insists on a *lowercase* letter before the punctuation, so "You have
// 5. he waves." leaves "he" alone. Both are ADRIFT's behaviour, not oversights here.
//
// [a-z] is ASCII in .NET too -- a character range, not a Unicode category -- so ADRIFT does not
// capitalise a sentence opening with 'é' either, and neither do we. That also means no byte this
// touches is ever part of a multi-byte UTF-8 character, since a continuation byte cannot match
// [a-z]: prose in any language passes through intact.
// Returns whether anything was raised.
bool AutoCapitalize(std::string &s) {
	// Written out by hand rather than handed to std::regex. ADRIFT rescans from the start of the
	// text after every letter it raises, which is quadratic in the length of a long message, and
	// this runs over every line the game prints. A single left-to-right pass gives the same answer:
	// raising a letter can only ever destroy a potential match (all three alternatives need a
	// *lowercase* letter where they act), never create an earlier one, so the matches come out in
	// the same order either way.
	auto isLower = [](char c) { return c >= 'a' && c <= 'z'; };
	bool changed = false;
	for (size_t i = 0; i < s.size(); i++) {
		size_t at = std::string::npos;
		if (i == 0 && isLower(s[0])) {
			at = 0;                                        // ^([a-z])
		} else if (s[i] == '\n' && i + 1 < s.size() && isLower(s[i + 1])) {
			at = i + 1;                                    // \n([a-z])
		} else if (isLower(s[i]) && i + 3 < s.size() && (s[i + 1] == '.' || s[i + 1] == '!' ||
				s[i + 1] == '?') && s[i + 2] == ' ') {     // [a-z][.!?] {1,2}([a-z])
			if (isLower(s[i + 3])) at = i + 3;
			else if (i + 4 < s.size() && s[i + 3] == ' ' && isLower(s[i + 4])) at = i + 4;
		}
		if (at == std::string::npos) continue;
		// The match guarantees an ASCII 'a'-'z', so raise it directly. std::toupper would ask
		// the locale, which the Qt frontend sets -- and a Turkish one does not raise 'i' to 'I'.
		s[at] = (char) (s[at] - 'a' + 'A');
		changed = true;
		// Nothing between here and the letter just raised can start a match of its own (it is
		// punctuation or a space), and the letter itself is no longer lowercase.
		i = at;
	}
	return changed;
}
}  // anonymous namespace

Game *Game::theGame = nullptr;
Game *Game::startupState = nullptr;
std::deque<Game::UndoRecord> Game::undoStates;
Game::UndoRecord Game::openRecord;
uint64_t Game::undoSeqCounter = 0;
uint64_t Game::undoGenerationCounter = 0;
bool Game::undoRecording = false;
bool Game::inputInFlight = false;

/* Copy constructor for game instances. Needs to create copies of all
 * the mutable game state objects.
 */
Game::Game(const Game &rhs) {
	// Slot for slot, so that every key already indexed against the original (see
	// GameStatic::objectIndex and friends) names the same thing in the copy.
	objects.reserve(rhs.objects.size());
	// For objects, we also need to respect subclassing...
	for (const GameObj *o : rhs.objects)
		objects.push_back(o->Clone());
	events.reserve(rhs.events.size());
	for (const Event *e : rhs.events)
		events.push_back(new Event(*e));
	variables.reserve(rhs.variables.size());
	for (const Variable *v : rhs.variables)
		variables.push_back(new Variable(*v));
	groups.reserve(rhs.groups.size());
	for (const Group *g : rhs.groups)
		groups.push_back(new Group(*g));
	descriptions.resize(rhs.descriptions.size(), nullptr);
	for (size_t i = 1; i < rhs.descriptions.size(); i++)  // slot 0 is the "none" sentinel
		descriptions[i] = new Description(*rhs.descriptions[i]);

	// just bools, so a vector copy is sufficient.
	taskFlagStorage = rhs.taskFlagStorage;

	// Finally, the simple data copies.
	playerKey = rhs.playerKey;
	mostRecentlyMentioned = rhs.mostRecentlyMentioned;
	pronounItText = rhs.pronounItText;
	pronounThemText = rhs.pronounThemText;
	pronounHimText = rhs.pronounHimText;
	pronounHerText = rhs.pronounHerText;
	lastCommand = rhs.lastCommand;
	turnCount = rhs.turnCount;
	gameHasBegun = rhs.gameHasBegun;
	sessionActive = rhs.sessionActive;
	restrictionsSoFar = rhs.restrictionsSoFar;
	textSnippetsSoFar = rhs.textSnippetsSoFar;
	expressionsSoFar = rhs.expressionsSoFar;
	staticData = rhs.staticData;
}

/* Destruct Game instance. This requires a bit of extra attention,
 * since we must not destruct any of the non-modifiable game content
 * (descriptions, restrictions) when we are not the currently-used
 * game state object.
 */
Game::~Game() {
	// destroy mutable game state
	// (C++ fun fact: it is indeed valid to `delete` a `const Foo *`.)
	for (const GameObj *o : objects)
		delete o;
	for (const Event *e : events)
		delete e;
	for (const Variable *v : variables)
		delete v;
	for (const Group *g : groups)
		delete g;
	for (auto *d : descriptions)
		delete d;

	if (theGame == this) {
		// We are the current (presumably last) game instance -- destroy everything.
		// (Should really only happen when shutting down the interpreter / loading a new game.)
		if (startupState != this)
			delete startupState;
		delete staticData;
	}
}

GameStatic::~GameStatic() {
	for (const auto &it: restrictions)
		delete it.second;
	for (const auto &it: properties)
		delete it.second;
	for (const auto &it: tasks)
		delete it.second;
	for (const auto &it: expressions)
		delete it.second;
	for (const auto &it: userFunctions)
		delete it.second;
	for (const auto &it: plainTextSnippets)
		delete it.second;
	for (const auto &it: synonyms)
		delete it.second;
	for (const auto &it: textOverrides)
		delete it.second;
}

const std::string &Game::GetPlayerLocationKey() const {
	return GetPlayerChar()->GetLocationKey();
}

void Game::SwitchPlayerCharacter(const std::string &newPlayerKey) {
	if (newPlayerKey == playerKey) return;
	static const std::vector<std::string> kFirstPersonPronouns = {"I", "me", "myself"};
	// Second person is treated as also answering to first-person pronouns, matching the original
	// ADRIFT runner (its comment there: "include 1st in 2nd").
	static const std::vector<std::string> kSecondPersonPronouns = {"I", "me", "myself", "you", "yourself"};
	const std::vector<std::string> *pronouns = nullptr;
	switch (staticData->pcReferralPerson) {
	case ReferralPerson::FirstPerson: pronouns = &kFirstPersonPronouns; break;
	case ReferralPerson::SecondPerson: pronouns = &kSecondPersonPronouns; break;
	case ReferralPerson::ThirdPerson: break;
	}
	if (pronouns) {
		if (GameObj *oldPlayer = MutableObject(playerKey))
			if (GameObj *newPlayer = MutableObject(newPlayerKey))
				oldPlayer->TransferPronounNouns(*newPlayer, *pronouns);
	}
	playerKey = newPlayerKey;
}

bool Game::PlayerIsInLocationOrGroup(const std::string &key) const {
	const std::string &here = GetPlayerLocationKey();
	// A key naming a location is only ever about that location, even if a group happens to share
	// the name: ADRIFT checks its locations first and stops there.
	if (AsLocation(IndexedGet(staticData->objectIndex, objects, key)))
		return here == key;
	if (const Group *grp = IndexedGet(staticData->groupIndex, groups, key))
		return grp->ContainsObj(here);
	return false;
}

size_t Game::TaskStateIndex(const std::string &key) const {
	auto it = staticData->tasks.find(key);
	if (it == staticData->tasks.end())
		throw std::out_of_range("no task with key '" + key + "'");
	return it->second->StateIndex();
}

void Game::PrepareUndoBookkeeping() {
	objectStamp.assign(objects.size(), 0);
	eventStamp.assign(events.size(), 0);
	variableStamp.assign(variables.size(), 0);
	groupStamp.assign(groups.size(), 0);
	descriptionStamp.assign(descriptions.size(), 0);
	taskFlagStamp.assign(taskFlagStorage.size(), 0);
}

// The five of these differ only in which table and which stamp array they touch; an object needs
// Clone() rather than a copy constructor because it may really be a Character or a Location.
void Game::PreserveObject(size_t slot) {
	if (!undoRecording || objectStamp[slot] == openRecord.generation) return;
	objectStamp[slot] = openRecord.generation;
	openRecord.objects.emplace_back((uint32_t) slot, objects[slot]->Clone());
}
void Game::PreserveEvent(size_t slot) {
	if (!undoRecording || eventStamp[slot] == openRecord.generation) return;
	eventStamp[slot] = openRecord.generation;
	openRecord.events.emplace_back((uint32_t) slot, new Event(*events[slot]));
}
void Game::PreserveVariable(size_t slot) {
	if (!undoRecording || variableStamp[slot] == openRecord.generation) return;
	variableStamp[slot] = openRecord.generation;
	openRecord.variables.emplace_back((uint32_t) slot, new Variable(*variables[slot]));
}
void Game::PreserveGroup(size_t slot) {
	if (!undoRecording || groupStamp[slot] == openRecord.generation) return;
	groupStamp[slot] = openRecord.generation;
	openRecord.groups.emplace_back((uint32_t) slot, new Group(*groups[slot]));
}
void Game::PreserveDescription(size_t slot) {
	if (!undoRecording || descriptionStamp[slot] == openRecord.generation) return;
	descriptionStamp[slot] = openRecord.generation;
	openRecord.descriptions.emplace_back((uint32_t) slot, new Description(*descriptions[slot]));
}
void Game::PreserveTaskFlag(size_t slot) {
	if (!undoRecording || taskFlagStamp[slot] == openRecord.generation) return;
	taskFlagStamp[slot] = openRecord.generation;
	openRecord.taskFlags.emplace_back((uint32_t) slot, taskFlagStorage[slot]);
}

void Game::ClearRecord(UndoRecord &rec) {
	for (auto &e : rec.objects) delete e.second;
	for (auto &e : rec.events) delete e.second;
	for (auto &e : rec.variables) delete e.second;
	for (auto &e : rec.groups) delete e.second;
	for (auto &e : rec.descriptions) delete e.second;
	rec.objects.clear();
	rec.events.clear();
	rec.variables.clear();
	rec.groups.clear();
	rec.descriptions.clear();
	rec.taskFlags.clear();
#ifdef SL_UNDO_AUDIT
	delete rec.shadow;
	rec.shadow = nullptr;
#endif
}

void Game::ApplyRecord(UndoRecord &rec) {
	// Written back through the pointers that already exist, so nothing in the engine that is
	// holding a piece of game content finds it moved underneath.
	for (auto &e : rec.objects) Unconst(objects[e.first])->AssignFrom(*e.second);
	for (auto &e : rec.events) *Unconst(events[e.first]) = *e.second;
	for (auto &e : rec.variables) *Unconst(variables[e.first]) = *e.second;
	for (auto &e : rec.groups) *Unconst(groups[e.first]) = *e.second;
	for (auto &e : rec.descriptions) *Unconst(descriptions[e.first]) = *e.second;
	for (auto &e : rec.taskFlags) taskFlagStorage[e.first] = e.second;

	playerKey = rec.playerKey;
	mostRecentlyMentioned = rec.mostRecentlyMentioned;
	pronounItText = rec.pronounItText;
	pronounThemText = rec.pronounThemText;
	pronounHimText = rec.pronounHimText;
	pronounHerText = rec.pronounHerText;
	lastCommand = rec.lastCommand;
	turnCount = rec.turnCount;
	gameHasBegun = rec.gameHasBegun;
	sessionActive = rec.sessionActive;
}

void Game::RestampOpenRecord() {
	openRecord.generation = ++undoGenerationCounter;
	for (auto &e : openRecord.objects) objectStamp[e.first] = openRecord.generation;
	for (auto &e : openRecord.events) eventStamp[e.first] = openRecord.generation;
	for (auto &e : openRecord.variables) variableStamp[e.first] = openRecord.generation;
	for (auto &e : openRecord.groups) groupStamp[e.first] = openRecord.generation;
	for (auto &e : openRecord.descriptions) descriptionStamp[e.first] = openRecord.generation;
	for (auto &e : openRecord.taskFlags) taskFlagStamp[e.first] = openRecord.generation;
}

#ifdef SL_UNDO_AUDIT
// A canonical text form of one slot's mutable state, taken through the same WriteState methods a
// save file goes through -- so it covers exactly the fields the game considers state rather than a
// second list of them kept by hand, which could quietly fall out of step with the first.
std::string Game::SlotFingerprint(const char *kind, size_t slot) const {
	std::string out;
	Save::Writer w(out);
	if (strcmp(kind, "object") == 0) objects[slot]->WriteState(w);
	else if (strcmp(kind, "event") == 0) events[slot]->WriteState(w);
	else if (strcmp(kind, "group") == 0) groups[slot]->WriteState(w);
	else if (strcmp(kind, "variable") == 0) {
		const Variable *v = variables[slot];
		if (v->HoldsText()) w.WriteKV("v", v->GetStrArray());
		else w.WriteKV("v", v->GetIntArray());
	} else if (strcmp(kind, "description") == 0) {
		w.WriteKV("v", descriptions[slot]->GetState());
	}
	return out;
}

// Check the world we have just restored in place against the whole-world copy taken when this
// record was opened. A missed preserve shows up here as a slot that differs, named, rather than as
// a wrong description three turns later.
void Game::AuditRestore(const UndoRecord &rec) const {
	if (!rec.shadow) return;
	const Game &want = *rec.shadow;
	auto complain = [&](const char *kind, size_t slot, const std::string &a, const std::string &b) {
		LogError(std::string("UNDO AUDIT: ") + kind + " slot " + std::to_string(slot) +
			" was not restored correctly.\n  got:  " + a + "\n  want: " + b);
		frontend->FatalError("Undo audit failed; see the log.");
	};
	for (size_t i = 0; i < objects.size(); i++) {
		std::string a = SlotFingerprint("object", i), b = want.SlotFingerprint("object", i);
		if (a != b) complain("object", i, a, b);
	}
	for (size_t i = 0; i < events.size(); i++) {
		std::string a = SlotFingerprint("event", i), b = want.SlotFingerprint("event", i);
		if (a != b) complain("event", i, a, b);
	}
	for (size_t i = 0; i < variables.size(); i++) {
		std::string a = SlotFingerprint("variable", i), b = want.SlotFingerprint("variable", i);
		if (a != b) complain("variable", i, a, b);
	}
	for (size_t i = 0; i < groups.size(); i++) {
		std::string a = SlotFingerprint("group", i), b = want.SlotFingerprint("group", i);
		if (a != b) complain("group", i, a, b);
	}
	for (size_t i = 1; i < descriptions.size(); i++) {
		std::string a = SlotFingerprint("description", i), b = want.SlotFingerprint("description", i);
		if (a != b) complain("description", i, a, b);
	}
	if (taskFlagStorage != want.taskFlagStorage)
		complain("task-completion", 0, "(differs)", "(differs)");
	if (playerKey != want.playerKey || turnCount != want.turnCount ||
			gameHasBegun != want.gameHasBegun || sessionActive != want.sessionActive ||
			mostRecentlyMentioned != want.mostRecentlyMentioned ||
			pronounItText != want.pronounItText || pronounThemText != want.pronounThemText ||
			pronounHimText != want.pronounHimText || pronounHerText != want.pronounHerText ||
			lastCommand != want.lastCommand)
		complain("session state", 0, playerKey, want.playerKey);
}
#endif  // SL_UNDO_AUDIT

void Game::SaveUndo() {
	// Everything the last turn changed is already in the open record, so closing it is all there
	// is to do. The new one starts out empty and remembers the scalars as they stand now.
	openRecord.seq = ++undoSeqCounter;
	undoStates.push_back(std::move(openRecord));

	openRecord = UndoRecord{};
	openRecord.generation = ++undoGenerationCounter;
#ifdef SL_UNDO_AUDIT
	// The world as it stands at this undo point. Applying the record now being opened is supposed
	// to reproduce exactly this, which is what AuditRestore checks -- so the copy belongs to the
	// record being opened, not to the one just closed.
	openRecord.shadow = new Game(*this);
#endif
	openRecord.playerKey = playerKey;
	openRecord.mostRecentlyMentioned = mostRecentlyMentioned;
	openRecord.pronounItText = pronounItText;
	openRecord.pronounThemText = pronounThemText;
	openRecord.pronounHimText = pronounHimText;
	openRecord.pronounHerText = pronounHerText;
	openRecord.lastCommand = lastCommand;
	openRecord.turnCount = turnCount;
	openRecord.gameHasBegun = gameHasBegun;
	openRecord.sessionActive = sessionActive;
	undoRecording = true;

	while (undoStates.size() > kMaxUndoStates)
		DiscardUndo();
}

bool Game::RestoreUndo() {
	if (undoStates.empty())
		return false;
	ApplyRecord(openRecord);
#ifdef SL_UNDO_AUDIT
	AuditRestore(openRecord);
#endif
	ClearRecord(openRecord);
	// The record closed at the point we have just come back to becomes the open one again, so
	// that anything written between now and the next turn is covered by the next UNDO too.
	openRecord = std::move(undoStates.back());
	undoStates.pop_back();
	RestampOpenRecord();
	return true;
}

void Game::Discard() {
	// Drop any undo history first: the objects it holds belong to the game being torn down.
	for (auto &state : undoStates)
		ClearRecord(state);
	undoStates.clear();
	ClearRecord(openRecord);
	openRecord = UndoRecord{};
	undoRecording = false;
	// ~Game() frees startupState itself (it's a snapshot sharing theGame's staticData), but only
	// the object it points to -- the static pointer would otherwise dangle rather than read as
	// "none yet", so a subsequently loaded game's first Begin() would skip creating its own.
	delete theGame;
	theGame = nullptr;
	startupState = nullptr;
}

void Game::DiscardUndo() {
	if (undoStates.empty())
		return;
	ClearRecord(undoStates.front());
	undoStates.pop_front();
}

void Game::AssignStateFrom(const Game &src) {
	// Slot for slot, writing through the pointers that already exist rather than replacing them.
	for (size_t i = 0; i < objects.size(); i++)
		MutableObject(i)->AssignFrom(*src.objects[i]);
	for (size_t i = 0; i < events.size(); i++)
		*MutableEvent(i) = *src.events[i];
	for (size_t i = 0; i < variables.size(); i++)
		*MutableVariable(i) = *src.variables[i];
	for (size_t i = 0; i < groups.size(); i++)
		*MutableGroup(i) = *src.groups[i];
	for (size_t i = 1; i < descriptions.size(); i++)  // slot 0 is the "none" sentinel
		*MutableDescription(i) = *src.descriptions[i];

	taskFlagStorage = src.taskFlagStorage;
	playerKey = src.playerKey;
	mostRecentlyMentioned = src.mostRecentlyMentioned;
	pronounItText = src.pronounItText;
	pronounThemText = src.pronounThemText;
	pronounHimText = src.pronounHimText;
	pronounHerText = src.pronounHerText;
	lastCommand = src.lastCommand;
	turnCount = src.turnCount;
	gameHasBegun = src.gameHasBegun;
	sessionActive = src.sessionActive;
}

void Game::Restart() {
	// Cleared before the state goes back, not after: a restart is a fresh start with nothing
	// before it to return to, and there is no point recording the way back to a game that is
	// being thrown away.
	for (auto &rec : undoStates)
		ClearRecord(rec);
	undoStates.clear();
	ClearRecord(openRecord);
	openRecord = UndoRecord{};
	undoRecording = false;

	AssignStateFrom(*startupState);
	Begin();
}

void Game::Begin() {
	if (!startupState)
		startupState = new Game(*this);
	std::fill(taskFlagStorage.begin(), taskFlagStorage.end(), (uint8_t) 0);
	// Every character has "seen" their initial surroundings.
	MarkVisibleThingsSeen();
	gameHasBegun = true;
	// System tasks set to run as the game starts do so now: the game is up, but nothing has been
	// played yet. Directly rather than through the arrival queue -- nobody has arrived anywhere,
	// and these run whatever location the player begins in. Before the intro and before events
	// start, as in ADRIFT.
	for (Task *t : staticData->runImmediatelyTasks) {
		ResponseScope scope(this);
		ExecuteTaskByKey(t->Key());
	}
	// ...and if one of those moved the player, whatever waits where they landed runs here rather
	// than being left to go off during the player's first command, which is where ADRIFT drains
	// its queue. Games use a start-up task to walk the player through every location in turn, to
	// mark them all as seen for the map, and any arrival triggers that walk trips are spurious
	// either way -- better they happen amid the start-up they belong to than in the middle of the
	// first real turn. (Grandpa does exactly this, tripping a music cue and its cancel.)
	RunTriggeredTasks();
	// A game with real-time events, on a frontend with no clock to drive them, would quietly lose
	// whole subsystems. Better to say so than to let the player wonder. Deliberately not degraded
	// to turn-based instead: a "fifteen seconds later" event turned into "fifteen turns later" is
	// a different game, not a lesser one. Said again on RESTART, which is right -- it is still true.
	if (!frontend->timersAvailable) {
		for (const Event *evt : events) {
			// Also true of a turn-based event with a seconds-measured subevent: that subevent
			// still needs a wall clock, even though the event around it doesn't.
			if (evt->IsRealTime() || evt->HasRealTimeSubEvents()) {
				frontend->OutputText("<i>This game uses real-time events, which this interpreter "
				                     "cannot run. Parts of it will not happen.</i>\n");
				break;
			}
		}
	}
	if (staticData->gameIntro != 0) {
		std::string intro = MutableDescription(staticData->gameIntro)->BuildAndCommit();
		StripSeamMarkers(intro);
		frontend->OutputText(intro.c_str());
	}
	// As in ADRIFT: the initial room description, if the game asks for one, follows the intro
	// rather than being left for the player's first LOOK.
	if (staticData->showFirstLocation) {
		if (auto *loc = AsLocation(TryGetObject(GetPlayerLocationKey()))) {
			std::string desc = "\n" + loc->GetDescription();
			StripSeamMarkers(desc);
			frontend->OutputText(desc.c_str());
		}
	}
	// Must come after taskFlagStorage is populated above: an immediate, zero-length event
	// fires its subevents from inside Start(), those run tasks, and a task asking whether it has
	// already been completed would find nothing to read. After the intro and initial room
	// description too, as in ADRIFT -- an event started immediately runs, and can print, only
	// once the game has finished introducing itself.
	for (size_t i = 0; i < events.size(); i++) {
		Event *evt = MutableEvent(i);
		switch (evt->GetStartType()) {
			case Event::StartType::TaskBased:
				evt->SetNotYetStarted();
				break;
			case Event::StartType::AfterDelay:
				evt->BeginCountdown();
				break;
			case Event::StartType::Immediately:
				// Forced: there is no event loop to be inside of yet, and an event told to start
				// immediately should not have to wait a turn for it.
				evt->Start(/*force =*/ true);
				break;
			case Event::StartType::Invalid:
				// "<WhenStart>0</WhenStart>", which ADRIFT's own enum has no value for and which
				// 36 events across the test games nonetheless carry. Taken to mean the event
				// never runs.
				break;
		}
	}
	// Deliberately a second pass, as in ADRIFT. Start() leaves the "started on this tick" flag
	// set so that an event doesn't also age on the tick it started; carried out of load and into
	// the first real tick, that would have every immediate event sit out turn one.
	for (size_t i = 0; i < events.size(); i++)
		if (events[i]->JustStarted()) MutableEvent(i)->ClearJustStarted();
	// Walks that begin active start now, after the events, as in ADRIFT. Starting one moves its
	// character to the walk's first step and may run its opening sub-walks.
	for (size_t i = 0; i < objects.size(); i++)
		if (auto *c = AsCharacter(MutableObject(i)))
			c->StartActiveWalks();
}

void Game::EndGame(Ending how) {
	if (!gameHasBegun) return;  // already over; the first ending is the one that counts
	// Queued rather than printed outright: the task that ended the game is still running, and its
	// own parting words -- the last thing the player reads -- have yet to be flushed. The banner
	// belongs after them.
	//
	// ADRIFT reaches CheckEndOfGame only after EvaluateInput has finished the command off with two
	// blank-line Displays, so its whole sign-off starts two line breaks below whatever the game
	// last said. We have no such per-command trailer -- the frontend's prompt stands in for it --
	// so the break is put here instead, where it is the same two lines to the player.
	RecordResponse("\n\n");
	switch (how) {
	case Ending::Win:
		RecordResponse("<center><c><b>*** You have won ***</b></c></center>\n");
		break;
	case Ending::Lose:
		RecordResponse("<center><c><b>*** You have lost ***</b></c></center>\n");
		break;
	case Ending::Neutral:
		// A neutral ending announces itself only through whatever the task itself said.
		break;
	}
	// A game that keeps score signs off with the final tally, as ADRIFT's CheckEndOfGame does --
	// gated on there being a maximum worth reporting, which is how a game that never scored
	// anything is spared a line of zeroes. Both figures are the game's own variables (ADRIFT's
	// Adventure.Score/MaxScore read straight through to them), so a game that doesn't define them
	// simply has no maximum and says nothing.
	const Variable *maxScore = GetVariable("MaxScore");
	if (maxScore && maxScore->GetType() == Variable::Type::Int && maxScore->GetValue<int64_t>() > 0) {
		const Variable *score = GetVariable("Score");
		const int64_t scored = score && score->GetType() == Variable::Type::Int
			? score->GetValue<int64_t>() : 0;
		RecordResponse("In that game you scored " + std::to_string(scored)
		               + " out of a possible " + std::to_string(maxScore->GetValue<int64_t>())
		               + ", in " + std::to_string(turnCount) + " turns.\n\n");
	}
	// ProcessInput keys off this: no more turns, no more events, and no more ordinary commands --
	// only RESTART, RESTORE, QUIT, and UNDO (see AttemptMatchEndOfGameCommand), which is exactly
	// what this question offers.
	RecordResponse("Would you like to <c>restart</c>, <c>restore</c> a saved game, <c>quit</c> or "
	               "<c>undo</c> the last command?\n\n");
	gameHasBegun = false;
}

void Game::TurnTick() {
	// An event's subevent can run a task, and that task can carry a "skip N turns" action, which
	// ticks straight back through here. Depth-limited rather than trusted: a game that manages to
	// tie that knot should misbehave, not take the interpreter down with it. (ADRIFT has no such
	// guard and simply exhausts its stack.)
	//
	// Counted by an object rather than a bare increment/decrement around the call below, because a
	// task action that throws unwinds straight past this frame -- and the frontend catches that and
	// carries on playing. A depth left un-decremented would be permanent, and the thirty-second
	// such throw would stop the world ticking for good, silently.
	static int depth = 0;
	if (depth >= kMaxTickDepth) return;
	struct DepthGuard {
		DepthGuard() { depth++; }
		~DepthGuard() { depth--; }
	} guard;
	RunEventTick(false);
}

void Game::MarkVisibleThingsSeen() {
	// ADRIFT sweeps every character against every object once per turn (PrepareForNextTurn) rather
	// than relying on things being noticed as they move. It has to: a turn can make something
	// visible without moving it -- opening the container it already sits in is the usual way -- and
	// an object nobody was ever recorded as seeing fails the "has been seen by" restrictions that
	// the standard library's TAKE and EXAMINE tasks are built on, leaving it unmentionable.
	for (size_t i = 0; i < objects.size(); i++) {
		if (auto *c = AsCharacter(MutableObject(i)))
			c->MarkVisibleAsSeen();
	}
}

void Game::Tick() {
	// See `inputInFlight`: a command is part-way through and its own turn tick hasn't happened
	// yet, so the world must not move underneath it.
	if (inputInFlight) return;
	RunEventTick(true);
	// Deliberately no SaveUndo(): a real-time tick is the world acting on its own, not the player
	// taking a turn, and ADRIFT doesn't snapshot on one either. UNDO after a few seconds' thought
	// should rewind the last thing the player *did*, not the seconds they spent thinking -- and
	// snapshotting here would grow the undo stack while they sat idle.
}

void Game::RunEventTick(bool realTime) {
	// The game can end part-way through a tick -- an event runs a task that wins or loses it --
	// and the rest of that tick's events then don't happen. ADRIFT bails the same way.
	if (!gameHasBegun) return;
	SL_DEBUG(Events, (realTime ? "real-time" : "turn") << " tick over " << events.size() << " event(s)");
	// Character walks advance here, ahead of the events and before the events-running flag goes up --
	// exactly where ADRIFT drives them. Only on the turn clock: walks have no real-time variety. In
	// object load order so two ticks of the same state move the same characters in the same sequence.
	if (!realTime) {
		for (size_t i = 0; i < objects.size(); i++) {
			if (!gameHasBegun) return;
			// Tested on the read-only pointer: asking for a writable one is what copies the object
			// into the undo record, and doing that for every object in the game every turn -- to
			// find the handful that are characters with walks -- was most of what a turn cost.
			const auto *c = AsCharacter(objects[i]);
			if (!c || !c->HasWalks()) continue;
			AsCharacter(MutableObject(i))->TickWalks();
		}
	}
	// A "skip N turns" action can land us back in here while an outer tick is still going, so
	// remember rather than assume what to put back -- and put it back from a destructor, since a
	// task action that throws unwinds past this frame while the frontend carries on playing.
	// Left stuck at true, every later event command would act at once instead of waiting its
	// turn; ADRIFT has this exact bug, and bails out of its own loop leaving the flag set.
	struct RunningGuard {
		Game *g; bool prev;
		explicit RunningGuard(Game *game) : g(game), prev(game->eventsRunning) { g->eventsRunning = true; }
		~RunningGuard() { g->eventsRunning = prev; }
	} runningGuard(this);
	for (size_t i = 0; i < events.size(); i++) {
		if (!gameHasBegun) break;
		const Event *evt = events[i];
		if (evt->IsRealTime() == realTime) {
			if (!evt->TickWouldDoNothing()) MutableEvent(i)->IncrementTimer();
		}
		// A turn-based event's seconds-measured subevents ride the wall clock on their own,
		// independently of the turn clock the rest of the event runs on -- so they get serviced
		// on the real-time pass even though the event itself doesn't tick there. Asked of the
		// read-only pointer first: this arm is offered every turn-based event in the game on every
		// wall-clock tick, and asking for a writable one is what copies it into the undo record.
		else if (realTime && evt->HasSubEventClockRunning())
			MutableEvent(i)->TickRealTimeSubEvents();
	}
	// Deliberately a second pass over the same events, as in ADRIFT. An event late in the order
	// can run a task that starts one earlier in the order, which has already had its tick; if the
	// "don't age on the turn you started" flag survived into the next tick, that event would sit
	// out a turn it ought to have counted.
	for (size_t i = 0; i < events.size(); i++) {
		if (events[i]->IsRealTime() == realTime && events[i]->JustStarted())
			MutableEvent(i)->ClearJustStarted();
	}
}

bool Game::Save() {
	auto hFile = frontend->CreateSaveFile();
	if (!hFile)  // no file -- assume user cancelled
		return false;
	Save::Writer writer(hFile, this);
	writer.WriteKV("player", playerKey);
	writer.WriteKV("turns", turnCount);

	// Everything below walks the state in load order, so that two saves of the same game state
	// produce the same bytes.
	writer.BeginNamedCompound("objects");
	for (const GameObj *obj: objects) {
		writer.BeginNamedCompound(obj->Key().c_str());
		obj->WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();

	writer.BeginNamedCompound("events");
	for (const Event *evt: events) {
		writer.BeginNamedCompound(evt->Key().c_str());
		evt->WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();

	writer.BeginNamedCompound("variables");
	for (const Variable *var: variables) {
		switch (var->GetType()) {
		case Variable::Type::Int:
		case Variable::Type::IntArray:
			writer.WriteKV(var->Key().c_str(), var->GetIntArray());
			break;
		case Variable::Type::String:
		case Variable::Type::StringArray:
			writer.WriteKV(var->Key().c_str(), var->GetStrArray());
			break;
		}
	}
	writer.EndCompound();

	writer.BeginNamedCompound("groups");
	// "no properties of its own" is the initial state, so only save anything for groups that have
	// diverged from it -- ContinueRestore resets every group to that state before applying the
	// file's exceptions, mirroring descriptions_shown just below.
	for (const Group *grp: groups) {
		if (!grp->HasOwnProperties()) continue;
		writer.BeginNamedCompound(grp->Key().c_str());
		grp->WriteState(writer);
		writer.EndCompound();
	}
	writer.EndCompound();

	writer.BeginNamedCompound("descriptions_shown");
	for (size_t i = 1; i < descriptions.size(); i++) {
		auto name = std::to_string(i);
		auto state = descriptions[i]->GetState();
		// "not shown" is the initial state, so only save anything for those descriptions where at least one segment has been shown.
		if (std::find(state.cbegin(), state.cend(), true) != state.cend())  // at least one true
			writer.WriteKV(name.c_str(), state);
	}
	writer.EndCompound();

	writer.BeginNamedCompound("tasks_completed", true);
	for (const auto &it: staticData->tasks) {
		if (GetIsTaskCompleted(it.first)) {
			writer.WriteLiteralString(it.first.c_str());
			writer.WriteUnqouted(" ");
		}
	}
	writer.WriteUnqouted("} ");  // sneaky! (Avoiding the trailing space added by `EndCompound`)

	writer.BeginNamedCompound("tasks_scored", true);
	for (const auto &it: staticData->tasks) {
		if (GetIsTaskScored(it.first)) {
			writer.WriteLiteralString(it.first.c_str());
			writer.WriteUnqouted(" ");
		}
	}
	writer.WriteUnqouted("}");
	return true;
}

bool Game::Restore() {
	auto hFile = frontend->OpenSaveFile();
	if (!hFile)  // no file -- assume user cancelled
		return false;
	Save::Parser sav(hFile);
	sav.Prepare();
	Save::AstNode *root;
	try {
		root = sav.Parse();
	} catch (Save::SaveFileError &e) {
		frontend->OutputText("<i>Restore failed: the selected save file appears to be invalid.</i>");
		return false;
	}
	if (root == nullptr) {
		frontend->OutputText("<i>Restore failed: the selected save file appears to be invalid.</i>");
		return false;
	}
	{
		auto *metaNode = root->FindChildByName("meta");
		if (!metaNode) return false;
		auto *versionNode = metaNode->FindChildByName("version");
		if (!versionNode) return false;
		if (versionNode->sv.Int != Save::currentSaveFileVer) {
			frontend->OutputText("<i>Restore failed: selected save file appears to have been created using a different version of Starlane.</i>");
			return false;
		}
		auto *gameTitleNode = metaNode->FindChildByName("game_title");
		auto *gameAuthorNode = metaNode->FindChildByName("game_author");
		if (!gameTitleNode || !gameAuthorNode) return false;
		if (gameTitleNode->Str != staticData->gameTitle || gameAuthorNode->Str != staticData->gameAuthor) {
			frontend->OutputText("<i>Restore failed: selected save file appears to belong to a different game.</i>");
			return false;
		}
		auto *gameRevNode = metaNode->FindChildByName("game_revision");
		auto *gameChecksumNode = metaNode->FindChildByName("game_checksum");
		if (!gameRevNode || !gameChecksumNode) return false;
		if (gameRevNode->Str != staticData->gameLastUpdated || gameChecksumNode->sv.Int != staticData->gameCrc32) {
			// todo: offer player the option to attempt restore anyways.
			frontend->OutputText("<i>Restore failed: selected save file appears to belong to a different revision of this game.</i>");
			return false;
		}
	}
	SaveUndo();  // so we can return in the event of a failure once game state has been modified
	// (SaveUndo files a copy away and leaves the current instance alone, so this is still `this`.)
	return ContinueRestore(root);
}

bool Game::ContinueRestore(const Save::AstNode *root) {
	{
		auto *playerNode = root->FindChildByName("player");
		if (!playerNode || playerNode->type != Save::NT_STRING) return RollbackRestore();
		playerKey = playerNode->Str;
	}
	{
		auto *turnsNode = root->FindChildByName("turns");
		if (!turnsNode || turnsNode->type != Save::NT_INT) return RollbackRestore();
		turnCount = (uint32_t) turnsNode->sv.Int;
	}
	{
		auto *objsNode = root->FindChildByName("objects");
		if (!objsNode || !objsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		ITERATE_CHILDREN(objsNode, objN) {
			// A save file naming something this game hasn't got is not one of ours.
			auto *obj = MutableObject(objN->myName);
			if (!obj || !obj->RestoreState(objN)) return RollbackRestore();
		}
	}
	{
		auto *evtsNode = root->FindChildByName("events");
		if (!evtsNode || !evtsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		ITERATE_CHILDREN(evtsNode, evtN) {
			auto *evt = MutableEvent(evtN->myName);
			if (!evt || !evt->RestoreState(evtN)) return RollbackRestore();
		}
	}
	{
		auto *varsNode = root->FindChildByName("variables");
		if (!varsNode || !varsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		ITERATE_CHILDREN(varsNode, varN) {
			auto *var = MutableVariable(varN->myName);
			if (!var) return RollbackRestore();
			size_t counter = 0;
			switch (var->GetType()) {
				case Variable::Type::Int:
				case Variable::Type::IntArray:
					if (!varN->IsCollection(Save::NT_INTLIST)) return RollbackRestore();
					ITERATE_CHILDREN(varN, varV) {
						var->SetValue(varV->sv.Int, ++counter);
					}
					break;
				case Variable::Type::String:
				case Variable::Type::StringArray:
					if (!varN->IsCollection(Save::NT_STRINGLIST)) return RollbackRestore();
					ITERATE_CHILDREN(varN, varV) {
						var->SetValue(varV->Str, ++counter);
					}
					break;
			}
		}
	}
	{
		const auto *grpsNode = root->FindChildByName("groups");
		if (!grpsNode || !grpsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		// Only groups with properties of their own get written out, so reset the lot to "none" and
		// let the file fill in the exceptions -- see the matching comment on descriptions_shown below.
		for (size_t i = 0; i < groups.size(); i++)
			MutableGroup(i)->ResetState();
		ITERATE_CHILDREN(grpsNode, grpN) {
			auto *grp = MutableGroup(grpN->myName);
			if (!grp || !grp->RestoreState(grpN)) return RollbackRestore();
		}
	}
	{
		const auto *descsNode = root->FindChildByName("descriptions_shown");
		if (!descsNode || !descsNode->IsCollection(Save::NT_COMPOUND)) return RollbackRestore();
		// Only descriptions with at least one segment shown get written out, so reset the lot
		// to "not shown" and let the file fill in the exceptions. The entries are written out
		// of an unordered_map, so they arrive in no particular order -- hence resetting up
		// front rather than filling the gaps between consecutive entries as we go.
		for (size_t i = 1; i < descriptions.size(); i++)
			MutableDescription(i)->RestoreState();
		ITERATE_CHILDREN(descsNode, descN) {
			const auto idx = (size_t) ParseInt(descN->myName.c_str());
			if (idx == 0 || idx >= descriptions.size()) return RollbackRestore();  // no such description
			std::vector<bool> state;
			ITERATE_CHILDREN(descN, entry) {
				state.push_back(entry->sv.Bool);
			}
			MutableDescription(idx)->RestoreState(state);
		}
	}
	{
		const auto *tasksCompletedNode = root->FindChildByName("tasks_completed");
		if (!tasksCompletedNode || !tasksCompletedNode->IsCollection(Save::NT_STRINGLIST)) return RollbackRestore();
		// Save() writes only the completed ones, as a bare list of keys -- so a task's presence
		// here is what says it is completed, and everything else stays at the zero filled in above.
		// (A string list's members carry their text in `Str` and have no `myName`.)
		std::fill(taskFlagStorage.begin(), taskFlagStorage.end(), (uint8_t) 0);
		ITERATE_CHILDREN(tasksCompletedNode, taskNode) {
			const auto *task = SafeMapGet(staticData->tasks, taskNode->Str);
			if (!task) return RollbackRestore();  // no such task
			taskFlagStorage[task->StateIndex()] |= kTaskCompleted;
		}
		// ...and the same again for the tasks that have already had their say on the score.
		const auto *tasksScoredNode = root->FindChildByName("tasks_scored");
		if (!tasksScoredNode || !tasksScoredNode->IsCollection(Save::NT_STRINGLIST)) return RollbackRestore();
		ITERATE_CHILDREN(tasksScoredNode, taskNode) {
			const auto *task = SafeMapGet(staticData->tasks, taskNode->Str);
			if (!task) return RollbackRestore();
			taskFlagStorage[task->StateIndex()] |= kTaskScored;
		}
	}
	// finally, discard all previous undo states because UNDOing a restore would be a bit silly
	while (Game::UndoAvailable())
		Game::DiscardUndo();
	return true;
}

bool Game::RollbackRestore() {
	RestoreUndo();
	frontend->OutputText("<i>Restore failed: selected save file is invalid.</i>");
	return false;
}

bool Game::GetStatusBar(StatusBar &statusBar) {
	if (!gameHasBegun) return false;
	try {
		auto locationName = GetObject(GetPlayerLocationKey())->GetDisplayName();
		ApplyOverrides(locationName);
		statusBar.location = locationName;
		auto userStatus = MutableDescription(staticData->userStatusBar)->BuildAndCommit();
		ApplyOverrides(userStatus);
		statusBar.userStatus = userStatus;
		// MaxScore is a variable and can theoretically be changed (although I'm not sure
		// the ADRIFT Runner would pick up on a game trying that)
		const auto *mscore = GetVarByName("MaxScore");
		if (mscore && mscore->GetValue<int32_t>() > 0) {
			if (const auto *score = GetVarByName("Score"); score) {
				statusBar.scoringUsed = true;
				statusBar.score = score->GetValue<int32_t>();
			} else {
				statusBar.scoringUsed = false;
			}
		} else statusBar.scoringUsed = false;
	} catch (std::exception &e) { return false; }
	return true;
}

void Game::ApplyOverrides(std::string &t) const {
	std::string initialText;
	do {
		initialText = t;
		for (const auto &it: staticData->textOverrides) {
			const auto &f = it.second->GetFrom();
			// An empty needle would match at every position forever; nothing sensible to do.
			if (f.empty())
				continue;
			// Resume the search past each insertion rather than restarting from the front, as
			// .NET's String.Replace (which ADRIFT uses) does: every original occurrence is
			// replaced once, left to right, and the inserted text is not itself rescanned.
			// Restarting from zero hangs the moment a replacement contains its own from-text --
			// and games ship overrides that map a string to itself ("~~~~" -> "~~~~", a colour
			// tag to the same tag), which would otherwise loop forever.
			size_t pos = 0;
			// An override may have been given no replacement text at all, in which case it simply
			// deletes what it matches -- there is no description to build for it.
			const DescrRef replacementRef = it.second->GetReplacement();
			while ((pos = t.find(f, pos)) != std::string::npos) {
				std::string replacement(replacementRef == 0 ? std::string()
					: Game::Get()->MutableDescription(replacementRef)->BuildAndCommit());
				t.replace(pos, f.size(), replacement);
				pos += replacement.size();
			}
		}
	} while (initialText != t);
}

void Game::OutputFiltered(std::string s) const {
	ApplyOverrides(s);
	// Capitalise only now, after the overrides: a replacement can land mid-sentence in one message
	// and start a sentence in the next, and only the finished text knows which. ADRIFT is explicit
	// about the ordering, and about looking again afterwards -- an author may well have written an
	// override to match text as the player finally sees it, capital and all.
	if (AutoCapitalize(s))
		ApplyOverrides(s);
	StripSeamMarkers(s);
	if (s.empty()) return;
	// Successive messages within one turn run together otherwise: ADRIFT's Display() puts two
	// spaces between them unless what came before already ended a line (see pSpace). A message
	// that is nothing but a line break is exempt -- ADRIFT skips pSpace for one outright ("sText
	// <> vbCrLf") -- which is what lets a game use a blank message purely to separate two others.
	const bool onlyNewlines = s.find_first_not_of('\n') == std::string::npos;
	if (turnHasOutput && !endsWithNewline && !onlyNewlines)
		frontend->OutputText("  ");
	frontend->OutputText(s.c_str());
	turnHasOutput = true;
	endsWithNewline = s.back() == '\n';
}

}
