#pragma once

#ifndef SLC_WALK_H
#define SLC_WALK_H

#include "../slc_private.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "utility.h"

namespace Starlane {

class Character;

// A Character Walk: a scripted patrol that moves a character between locations on a schedule, and
// can display messages or run tasks along the way. Owned by a Character (each may have several) and
// modelled closely on Event -- the two share a lifecycle (start/stop/pause/resume, task-driven
// controls, a countdown clock) and much of the same reasoning. Where they differ: a walk's clock is
// ADRIFT's own "turns until the end", kept as-is here because a walk's steps and sub-walks are all
// expressed in terms of it, and because that is what a save file already carries. Walks are always
// turn-based; there is no real-time variety.
class Walk {
public:
	// Build a walk from a <Walk> element, remembering which character owns it -- the walk resolves
	// that character afresh through the live Game whenever it needs to move it, rather than holding a
	// pointer that a clone-for-undo would leave aimed at the previous Game's character.
	static Walk CreateFromXML(const pugi::xml_node &walkNode, const std::string &ownerKey);

	// Wire this walk's controls up to the tasks that start and stop it, so that completing one
	// reaches the walk. Deferred to a load pass of its own because characters load before tasks do,
	// so the tasks a control names don't yet exist while the character is being built. `selfIndex` is
	// this walk's position in its owner's list, which is how a task addresses it back.
	void RegisterNotifications(int32_t selfIndex) const;

	bool IsStartActive() const { return startActive; }

	// Start/stop/pause/resume this walk. Unlike an event, a walk always defers to its own next tick
	// (ADRIFT queues the command and applies it at the top of the following IncrementTimer); `force`
	// starts it right now, for the one caller with no tick to wait for -- Game::Begin starting the
	// walks that begin active.
	void Start(bool force = false);
	void Stop();
	void Pause();
	void Resume();

	// Advance this walk by one turn: apply any queued command, act on whichever step and sub-walks
	// this turn calls for, then move the clock on. Driven from Game's turn tick, ahead of the events.
	void IncrementTimer();

	// A task this walk listens for has completed or uncompleted; apply whatever controls named it.
	void ReceiveTaskNotification(Util::Control::Condition cond, const std::string &taskKey);

	void WriteState(Save::Writer &writer) const;
	bool RestoreState(const Save::AstNode *node);

	enum class Status {
		NotYetStarted,
		Running,
		Paused,
		Finished
	};

private:
	Walk() = default;

	struct Step {
		// Where this step sends the character. A location key moves them there outright; a group key
		// wanders them to a (preferably adjacent) member room; a character key follows that character
		// if adjacent; "%Player%" follows the player; "Hidden" takes them off the map.
		std::string location;
		// How many turns the character lingers here before the next step -- so also this step's
		// contribution to the walk's total length. A range is re-rolled at each (re)start.
		Util::Range turns;
	};

	enum class SubWhen {
		FromLastSubWalk,
		FromStartOfWalk,
		BeforeEndOfWalk,
		ComesAcross
	};
	enum class SubWhat {
		DisplayMessage,
		ExecuteTask,
		UnsetTask
	};

	struct SubWalk {
		SubWhen when;
		// Defaults to DisplayMessage, the state a sub-walk with no <Action> is left in (see the
		// loader) -- a no-op, since its description is then empty too.
		SubWhat what = SubWhat::DisplayMessage;
		// When to fire, for every `when` but ComesAcross (which fires on meeting the player instead).
		Util::Range turns;
		// The object ADRIFT's editor recorded for a ComesAcross sub-walk. Kept for fidelity but not
		// consulted at run time: ADRIFT hard-codes the meeting to the player regardless of what this
		// names, and so do we.
		std::string comesAcrossKey;
		// A DisplayMessage's text.
		DescrRef descr = 0;
		// The task an ExecuteTask/UnsetTask sub-walk acts on.
		std::string taskKey;
		// A DisplayMessage only shows while the player is at this location or group; empty means it
		// never shows (ADRIFT tests the key before it tests the player's whereabouts).
		std::string onlyAtLocation;
		// ComesAcross fires on the rising edge of "the character is where the player is" -- this
		// remembers the previous side of that edge from one tick to the next.
		bool sameLocationAsChar = false;
	};

	// A command queued by Start/Stop/Pause/Resume, applied at the top of the next IncrementTimer.
	enum class Command {
		None,
		Start,
		Stop,
		Pause,
		Resume,
		// Start() collapses to this when a Stop is already queued, so a stop-then-start in one turn
		// restarts a running walk rather than being swallowed.
		Restart
	};

	// The bodies behind Start/Stop/Pause/Resume, run once the deferral above is settled.
	void StartImpl(bool restart);
	void StopImpl(bool runSubWalks, bool reachedEnd);
	void PauseImpl();
	void ResumeImpl();

	// Resolve this walk's owning character through the live Game. Never null in practice: a walk only
	// exists as a member of the character named here.
	Character *Owner() const;

	// Assign the countdown, and act on it landing on zero: a running walk that reaches the end of its
	// clock stops (and loops, if set to). Every write goes through here, mirroring ADRIFT's property
	// setter, so that transition can never be missed.
	void SetTimerToEnd(int32_t t);

	// Total turns one pass of the walk takes: the sum of every step's rolled length.
	int32_t Length() const;
	// Turns elapsed since the walk started -- 0 on the tick it starts, counting up to Length. Derived
	// from the countdown exactly as ADRIFT derives it.
	int32_t TimerFromStartOfWalk() const { return Length() - timerToEnd + 1; }
	// Turns since the most recent sub-walk ran (or since the walk started, if none has).
	int32_t TimerFromLastSubWalk() const { return TimerFromStartOfWalk() - lastSubWalkTime; }
	// Re-roll every step's length, at each (re)start of the walk.
	void ResetLength();

	// Move the character to whichever step this turn's elapsed time lands on, announcing the move to
	// a watching player. Does nothing unless the walk is running.
	void DoAnySteps();
	// Print ADRIFT's "<name> exits to the north." / "enters from the south." line, if the character
	// carries the ShowEnterExit property and the player is placed to witness the move. `dest` is
	// where the character is about to go; call before actually moving it.
	void AnnounceMove(Character &owner, const std::string &dest) const;
	// A uniformly-random member of an ordered set, for a group-wandering step.
	static const std::string &PickRandomMember(const std::set<std::string> &members);
	// Fire whichever sub-walks this turn calls for. Does nothing unless the walk is running.
	void DoAnySubWalks();
	void RunSubWalk(int32_t idx);

	// The reference word closing a numeric sub-walk <When> ("... FromStartOfWalk").
	static SubWhen ParseSubWhen(const char *txt);

	// Content, fixed at load time.
	std::string ownerKey;
	std::string description;
	bool loops = false;
	bool startActive = false;
	std::vector<Util::Control> controls;
	std::vector<Step> steps;
	std::vector<SubWalk> subwalks;

	// Runtime state.
	Status status = Status::NotYetStarted;
	// Turns remaining before the walk ends. ADRIFT stores exactly this, and derives the elapsed time
	// from it; a save file carries it too.
	int32_t timerToEnd = 0;
	// TimerFromStartOfWalk as of the last sub-walk that ran, which is what "N turns after the last
	// sub-walk" counts from.
	int32_t lastSubWalkTime = 0;
	// Which sub-walk ran most recently, or -1 if none has since the walk last started. An index
	// rather than a pointer so that cloning a character wholesale for undo can't leave it dangling.
	int32_t lastSubWalkIndex = -1;
	Command nextCommand = Command::None;
};

}

#endif  // !SLC_WALK_H
