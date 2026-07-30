#pragma once

#ifndef SLC_EVENT_H
#define SLC_EVENT_H

#include "../slc_private.h"

#include <string>
#include <vector>

#include "utility.h"

namespace Starlane {

class Event {
public:
	static Event *CreateFromXML(const pugi::xml_node &xmlNode);

	[[nodiscard]] const std::string &Key() const { return key; }

	enum class StartType {
		Invalid,
		Immediately,
		// "Between X and Y turns": waits out `startDelay` before it begins. Named for what it
		// does rather than for ADRIFT's "BetweenXandYTurns", and deliberately not "TimeBased",
		// which would read as the opposite of TimeType::Turns -- a different axis entirely.
		AfterDelay,
		TaskBased
	};
	static StartType ParseStartType(const char *txt);
	StartType GetStartType() const { return startType; }

	enum class TimeType {
		Turns,
		RealTime
	};
	static TimeType ParseTimeType(const char *txt);

	enum class SEType {
		DisplayMessage,
		SetLook,
		ExecuteTask,
		UnsetTask  // that is, remove the "task completed" flag.
	};
	static SEType ParseSEType(const char *txt);

	enum class SERefType {
		EventBegin,
		LastSubEvent,
		EventEnd
	};
	static SERefType ParseSERefType(const char *txt);

	class Subevent {
	public:
		Util::Range when;
		SERefType whenRefType;
		TimeType timeType;
		SEType actionType;
		std::string actionTask;
		DescrRef actionDescr;
		std::string onlyAtLocation;

		// Seconds left before this subevent fires, counted down by TickRealTimeSubEvents. Only
		// meaningful for a seconds-measured subevent belonging to a turn-based event -- such a
		// subevent needs a clock of its own, since the event it belongs to only advances once a
		// player turn. -1 means "not currently counting down" (not due to start yet, or the event
		// isn't running).
		int32_t secondsRemaining = -1;
	};

	void ReceiveTaskNotification(Util::Control::Condition ctrl, const std::string &taskKey);

	// Start/stop/pause/resume this event at a task's request.
	//
	// Whether that happens now or on this event's next tick depends on where the asking task was
	// run from. A task run by another event's subevent -- i.e. from inside the event loop -- gets
	// its way immediately, because the events around it are being ticked right now and would
	// otherwise see a stale picture. A task the player typed does not: it waits, so that an event
	// started by this turn's command doesn't also get this turn's progress out of it. ADRIFT
	// draws the same line, and puts it plainly: "If an event runs a task and that task
	// starts/stops an event, do it immediately."
	//
	// `force` starts an event regardless, for the one caller with no event loop to be inside of:
	// Game::Begin.
	void Start(bool force = false);
	void Stop();
	void Pause();
	void Resume();

	// Put this event into the state Game::Begin's start-up pass calls for. Separate from Start()
	// because neither of these is a request from a task: they set up the event's initial state
	// rather than asking it to do anything.
	void SetNotYetStarted() { state = State::NotYetStarted; }
	// Begin waiting out `startDelay` before starting, for an event that starts after a delay.
	void BeginCountdown();

	// Whether any of this event's seconds-measured subevents is actually counting down right now.
	// Exactly the condition under which TickRealTimeSubEvents does anything -- and that is offered
	// to every turn-based event on every wall-clock tick, so asking first is what keeps a tick from
	// recording every one of them for undo to change nothing. Most games have no such subevent at
	// all: ADRIFT only arms a clock here for a *turn-based* event with a *seconds* subevent, which
	// is a combination none of the test games uses.
	bool HasSubEventClockRunning() const {
		if (state != State::Running) return false;
		for (const auto &se : subevents)
			if (se.secondsRemaining >= 0) return true;
		return false;
	}
	// Whether this event is driven by the wall clock rather than by turns. The only thing that
	// tells the two populations of events apart; both tick through IncrementTimer.
	bool IsRealTime() const { return timeType == TimeType::RealTime; }
	// Advance this event by one tick of whichever clock it runs on.
	void IncrementTimer();
	// Advance the private clocks of any seconds-measured subevents this (turn-based) event has.
	// Called on every wall-clock tick regardless of what this event's own clock runs on, since a
	// subevent counted in seconds rides the wall clock even when its event doesn't. A no-op for a
	// real-time event, whose seconds-measured subevents ride the event's own clock instead (see
	// DoAnySubEvents), and for one with none of its own.
	void TickRealTimeSubEvents();
	// Whether any subevent here is measured in seconds. Used to warn about a turn-based event
	// whose subevents still need a wall clock this interpreter might not have.
	bool HasRealTimeSubEvents() const;
	// The text this event currently wants LOOK (and the room description) to show in place of
	// wherever it applies, or empty if it isn't asking for an override right now. Mirrors
	// ADRIFT's own LookText(): live only while this event is Running, and reading back the
	// most-recently-pushed SetLook entry whose place the player is currently in -- entries stay
	// on the stack (see RunSubEvent's SetLook case) even once this event stops, so restarting it
	// does not forget what an earlier run had already set.
	std::string LookOverrideText() const;
	// Clear the "started on this very tick" flag. Done in a pass of its own once every event has
	// ticked, rather than by the event itself -- see Game::RunEventTick.
	void ClearJustStarted() { justStarted = false; }
	// Whether this event started on the tick now finishing -- what ClearJustStarted clears. Lets a
	// caller skip the call entirely when there is nothing to clear.
	bool JustStarted() const { return justStarted; }
	// Whether ticking this event could do anything at all. A game's events are mostly dormant at
	// any given moment, and every one of them is offered a tick twice per turn.
	bool TickWouldDoNothing() const {
		return nextCommand == Command::None && !justStarted &&
			(state == State::NotYetStarted || state == State::Paused || state == State::Finished);
	}

	// How long this event runs for, in ticks of its own clock. Not a plain accessor: a duration
	// written "3 to 7 turns" is a roll that gets settled the first time anyone asks, and the
	// settled value is saved state -- so on that one path this has to go back through the game for
	// an Event it may write to. Replaces a GetDuration() that handed out a mutable reference and
	// let a read-shaped expression ("%event.Length%") quietly change the world.
	uint32_t Length() const;
	int32_t GetTimeSinceStart() const { return timeSinceStart; }

	void WriteState(Save::Writer &writer) const;
	bool RestoreState(const Save::AstNode *node);

private:
	Event() = default;

	enum class State {
		NotYetStarted,
		CountingDownToStart,
		Running,
		Paused,
		Finished
	};
	// A start/stop/pause/resume asked for by a task run outside the event loop, remembered until
	// this event's next tick. See Start().
	enum class Command {
		None,
		Start,
		Stop,
		Pause,
		Resume
	};

	// The bodies behind Start/Stop/Pause/Resume, run once the deferral above has been settled.
	void StartImpl(bool restart);
	void StopImpl(bool runSubEvents);
	void PauseImpl();
	void ResumeImpl();
	// Move the clock, and act on where it lands: reaching the start of a countdown starts the
	// event, and running out of time ends it. Every write to timeSinceStart goes through here --
	// ADRIFT hangs the same two transitions off assignment to its own counter.
	void SetTimeSinceStart(int32_t t);
	// Run whichever subevents this tick calls for. Does nothing unless the event is running.
	void DoAnySubEvents();
	void RunSubEvent(int32_t idx);
	// (Re)start the private clocks of this event's seconds-measured subevents, called whenever
	// this (turn-based) event starts or restarts. Only the subevents that are due to start right
	// away do so here -- FromStartOfEvent ones, and the first FromLastSubEvent one, if any; a
	// later FromLastSubEvent one starts its own clock only once the one before it in the list has
	// run, from RunSubEvent.
	void StartRealTimeSubEvents();
	// Settle subevents[idx]'s roll and start its clock, or run it right away if that roll comes up
	// zero -- the seconds equivalent of a "0 turns from the start" subevent firing inline.
	void BeginSubEventCountdown(int32_t idx);

	// Ticks remaining before this event ends. ADRIFT stores this and derives the elapsed time; we
	// do it the other way round, because the elapsed time is what the `Position` expression
	// function reports and what a save file already carries.
	// CurrentState() rather than Value(): the latter settles the roll, and so is not const. Every
	// path that puts this event on the clock (StartImpl, BeginCountdown) settles `duration` first,
	// so by the time anything asks how long is left, there is a real answer to give.
	int32_t TimeToEnd() const { return (int32_t) duration.CurrentState() - timeSinceStart; }
	// Ticks since the last subevent ran (or since the event started, if none has).
	int32_t TimeSinceLastSubEvent() const { return timeSinceStart - lastSubEventTime; }

	std::string key;
	std::vector<Util::Control> controls;
	std::vector<Subevent> subevents;
	// Mutable at runtime, and saved: StartImpl rewrites Immediately into AfterDelay and keeps it.
	StartType startType;
	TimeType timeType;
	Util::Range duration;
	// <StartDelay>, which ADRIFT only writes for an event that starts after one. No test game
	// has one, but a repeating event that counts down again between runs reads it too.
	Util::Range startDelay{(uint32_t) 0};
	bool repeating;
	bool repeatCountdown;
	State state = State::NotYetStarted;
	// Ticks since this event started, and negative while counting down to one: -3 means "starts
	// in three ticks". ADRIFT counts the same span from the other end.
	int32_t timeSinceStart = 0;
	// An event that started on this very tick does not also age on it.
	bool justStarted = false;
	Command nextCommand = Command::None;
	// The last task whose completion triggered a control of ours this cycle (empty if none). ADRIFT's
	// sTriggeringTask: a control is ignored when its task's own Specific child already triggered us,
	// so a child task can't re-fire what the parent's completion is about to handle. Reset when the
	// queued command is applied in IncrementTimer, matching ADRIFT.
	std::string triggeringTask;
	// Which subevent ran most recently, or -1 if none has since this event last started. An index
	// rather than a pointer because an Event is cloned wholesale into an undo record, with
	// the compiler's own copy constructor -- a pointer would survive that copy aimed squarely at
	// the previous Game's subevent.
	int32_t lastSubEventIndex = -1;
	// timeSinceStart as of that subevent, which is what "N turns after the last subevent" counts
	// from.
	int32_t lastSubEventTime = 0;
	// Every SetLook subevent this event has ever run, in the order it ran them: (place, text).
	// Pushed to by RunSubEvent and never popped, exactly like ADRIFT's stackLookText -- see
	// LookOverrideText for how the most recent applicable entry gets read back out.
	std::vector<std::pair<std::string, std::string>> lookOverrides;
};

}

#endif  // !SLC_EVENT_H