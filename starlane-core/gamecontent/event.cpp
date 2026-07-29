#include "event.h"

#include <algorithm>
#include <set>
#include <utility>

#include <magic_enum.hpp>
#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"
#include "../savefiles/writer.h"
#include "../savefiles/parser.h"
#include "description.h"
#include "task.h"

// An event's schedule is otherwise almost impossible to observe: most of what events do is run
// tasks, whose own output says nothing about which event ran them or when. Follows the same
// pattern as gameloader.cpp's load-stage tracing.
#ifndef NDEBUG
#include <iostream>
// `what` is pasted into the '<<' chain unparenthesized, so that callers can go on chaining onto
// it. Anything binding more loosely than '<<' -- a ternary, say -- needs parenthesizing by the
// caller.
#define EVENT_TRACE(what) \
	std::cerr << "[evt] " << key << ' ' << what << " t=" << timeSinceStart \
	          << '/' << (int32_t) duration.CurrentState() << '\n'
#else
#define EVENT_TRACE(what) ((void) 0)
#endif

namespace Starlane {

Event *Event::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Event;
	result->key = xmlNode.child_value("Key");
	result->startType = ParseStartType(xmlNode.child_value("WhenStart"));
	result->timeType = ParseTimeType(xmlNode.child_value("Type"));
	result->repeating = ParseBool(xmlNode.child_value("Repeating"));
	result->repeatCountdown = xmlNode.child("RepeatCountdown").type() != pugi::node_null;
	result->duration = Util::Range(xmlNode.child_value("Length"));
	// ADRIFT only writes this out for an event that starts after a delay, so it is absent far
	// more often than not -- and absent means no delay at all.
	result->startDelay = Util::Range(xmlNode.child_value("StartDelay"));

	// Every event's controls are read, whatever its start type says. ADRIFT loads them
	// unconditionally too, and its CompleteTask walks every event's controls without once asking
	// how that event starts -- which is what lets an event with no start type of its own (Alyas
	// writes "<WhenStart>0</WhenStart>", a value ADRIFT's own enum has no name for, so it never
	// starts by itself) exist purely to be switched on by a task. Reading these only for
	// AfterATask events left the Temple's priests waiting on a control nothing was subscribed to.
	{
		for (const auto &it: xmlNode.children("Control")) {
			// Discount tokenization. Great.
			Util::Control c;
			const char *ctrlTxt = it.child_value();
			const char *x;
			if ((x = SkipText(ctrlTxt, "Start "))) {
				c.action = Util::Control::Action::Start;
			} else if ((x = SkipText(ctrlTxt, "Stop "))) {
				c.action = Util::Control::Action::Stop;
			} else if ((x = SkipText(ctrlTxt, "Suspend "))) {
				c.action = Util::Control::Action::Pause;
			} else if ((x = SkipText(ctrlTxt, "Resume "))) {
				c.action = Util::Control::Action::Resume;
			} else throw std::runtime_error(std::string("Invalid event control action text: ") + ctrlTxt);
			// `y`, rather than assigning back into `x`: SkipText returns null when the needle
			// isn't at offset 0, so a failing first arm would wipe out the very remainder the
			// second arm needs to look at. Together with the second arm having been handed
			// `ctrlTxt` (the whole "Stop Uncompletion Task1") instead of the remainder, that
			// meant both arms always failed and any game with an Uncompletion control threw on
			// load. No test game has one, and ADRIFT never acts on one either (it only ever
			// tests for Completion), so nothing ever noticed.
			const char *y;
			if ((y = SkipText(x, "Completion "))) {
				c.condition = Util::Control::Condition::Completion;
			} else if ((y = SkipText(x, "UnCompletion "))) {
				// ADRIFT's own spelling, capital C and all -- it writes the enum name verbatim. (The
				// old "Uncompletion" never matched; no event control in any test game is an
				// uncompletion, so it stayed latent -- but the walk equivalent does have one, which is
				// how the miscased needle came to light.)
				c.condition = Util::Control::Condition::Uncompletion;
			} else throw std::runtime_error(std::string("Invalid control condition text: ") + ctrlTxt);
			c.taskName = y;
			result->controls.emplace_back(c);
		}
		// Subscribe to every task our controls listen for, so that completing one reaches us.
		// Tasks are all loaded by the time any event is, so they can be looked up right here.
		//
		// Once per task and condition, however many controls name that pair: ReceiveTaskNotification
		// walks all of its matching controls itself, so subscribing twice would run each of them
		// twice over. A control naming a task that doesn't exist can never fire, so there is
		// nothing to hook it to.
		std::set<std::pair<std::string, Util::Control::Condition>> subscribed;
		for (const auto &c : result->controls) {
			if (!subscribed.emplace(c.taskName, c.condition).second) continue;
			if (Task *t = Game::Get()->GetTask(c.taskName))
				t->RegisterNotification(result->key, c.condition);
		}
	}

	for (const auto &it: xmlNode.children("SubEvent")) {
		Subevent se;
		{  // "<When>1 FromStartOfEvent</When>", or "<When>1 to 7 FromStartOfEvent</When>".
			// ADRIFT writes the reference type as the last space-separated token, so split there.
			// The old version instead scanned forward over the characters a range happens to be
			// made of (digits, spaces, and the letters of "to"), which stopped on the 'F' and so
			// handed Util::Range a range text with a trailing space -- enough to send it down its
			// broken path and leave the range's upper bound indeterminate.
			std::string whenTxt = it.child_value("When");
			auto sp = whenTxt.find_last_of(' ');
			if (sp == std::string::npos)
				throw std::runtime_error("Malformed subevent When: " + whenTxt);
			se.when = Util::Range(whenTxt.substr(0, sp).c_str());
			se.whenRefType = ParseSERefType(whenTxt.c_str() + sp + 1);
		}
		se.actionType = ParseSEType(it.child_value("What"));
		se.timeType = ParseTimeType(it.child_value("Measure"));
		switch (se.actionType) {
			case SEType::SetLook:
			case SEType::DisplayMessage:
				se.actionDescr = Game::Get()->CreateDescFromXML(it.child("Action"));
				se.onlyAtLocation = it.child_value("OnlyApplyAt");
				break;
			case SEType::ExecuteTask:
				se.actionTask = SkipText(it.child_value("Action"), "ExecuteTask ");
				break;
			case SEType::UnsetTask:
				se.actionTask = SkipText(it.child_value("Action"), "UnsetTask ");
				break;
		}
		result->subevents.emplace_back(se);
	}

	return result;
}

// A task asking for something gets it now if we are already mid-tick, and otherwise has to wait
// for our next one. See the declaration of Start() for why.
#define DEFER_OR(cmd, impl) \
	do { \
		if (Game::Get()->AreEventsRunning()) impl; \
		else nextCommand = Command::cmd; \
	} while (0)

void Event::Start(bool force) {
	if (force) { StartImpl(false); return; }
	DEFER_OR(Start, StartImpl(false));
}

void Event::Stop() { DEFER_OR(Stop, StopImpl(false)); }
void Event::Pause() { DEFER_OR(Pause, PauseImpl()); }
void Event::Resume() { DEFER_OR(Resume, ResumeImpl()); }

#undef DEFER_OR

void Event::BeginCountdown() {
	state = State::CountingDownToStart;
	startDelay.Reset();
	duration.Reset();
	duration.Value();  // settle the roll now, so TimeToEnd has an answer to give from here on
	// Negative: we are this many ticks *before* the start. Counting up to zero starts the event,
	// which SetTimeSinceStart sees to -- including right now, if there is no delay to wait out.
	SetTimeSinceStart(-(int32_t) startDelay.Value());
}

void Event::SetTimeSinceStart(int32_t t) {
	timeSinceStart = t;
	// Counted all the way down: begin. Straight to StartImpl rather than through Start(), since
	// this is the clock arriving rather than a task asking, and there is nothing to defer to.
	if (state == State::CountingDownToStart && timeSinceStart == 0)
		StartImpl(false);
	// Run out of time: end. This is the only path on which a repeating event repeats, which is
	// what stops one that a task stopped early from starting itself up again.
	if (state == State::Running && TimeToEnd() == 0)
		StopImpl(true);
}

void Event::StartImpl(bool restart) {
	// ADRIFT logs "Can't Start an Event that isn't waiting!" and does nothing.
	if (!(state == State::NotYetStarted || state == State::CountingDownToStart ||
			state == State::Finished || (state == State::Running && restart)))
		return;
	state = State::Running;
	// A "takes 3 to 7 turns" event rolls afresh every time it starts, rather than being stuck
	// with whatever it rolled the first time.
	duration.Reset();
	duration.Value();  // settle it, so TimeToEnd works for the rest of this run
	lastSubEventIndex = -1;
	lastSubEventTime = 0;
	for (auto &se : subevents)
		se.when.Reset();
	// Plain assignment, purely so the trace below reports the clock we are starting from; the
	// line after puts the same value through the transitions.
	timeSinceStart = 0;
	// After timeSinceStart settles to 0, not before: a subevent whose roll comes up zero runs
	// inline from within here, and records timeSinceStart as when it ran.
	StartRealTimeSubEvents();
	EVENT_TRACE((restart ? "restart" : "start"));
	// May end the event here and now: an event with no length has no time left the instant it
	// starts, so this trips the "run out of time" arm above, which runs the 0-turn subevents and
	// finishes the event before we get back.
	SetTimeSinceStart(0);
	// ...and if it didn't, the 0-turn subevents still get their chance. A no-op when the line
	// above already finished us off, since DoAnySubEvents only acts on a running event.
	if (timeSinceStart == 0) DoAnySubEvents();
	// ADRIFT rewrites the event's own start type here, and keeps the rewrite. It has to: the
	// 0-turn test in DoAnySubEvents refuses to fire while the start type still says Immediately,
	// so without this a repeating "immediately, 0 turns from the start" event -- the ordinary way
	// to say "every turn", and the whole of skybreak's status, death and mission machinery --
	// would never fire once. Hence also its presence in save files.
	if (startType == StartType::Immediately) startType = StartType::AfterDelay;
	justStarted = true;
}

void Event::StopImpl(bool runSubEvents) {
	// End-of-event subevents get their turn before we call this finished; it is the only chance
	// an "N turns before the end" subevent ever gets.
	if (runSubEvents) DoAnySubEvents();
	// ADRIFT declines to stop a paused event -- but only after the subevents above, which are
	// gated on the event running and so do nothing here anyway. Kept in this order to match.
	if (state == State::Paused) return;
	state = State::Finished;
	EVENT_TRACE("stop");
	// Repeat only on running out of time. A task that stopped us early leaves time on the clock,
	// and ADRIFT tests for exactly that, so an event stopped early stays stopped.
	if (repeating && TimeToEnd() == 0) {
		if (duration.CurrentState() > 0) {
			if (repeatCountdown)
				BeginCountdown();
			else
				StartImpl(true);
		}
		// else: a zero-length repeating event would restart, expire on the spot, restart... and
		// never hand control back. ADRIFT bails out here for the same reason, and axe-of-kolt has
		// three such events, so this is not hypothetical.
	}
}

void Event::PauseImpl() {
	if (state == State::Running)
		state = State::Paused;
}

void Event::ResumeImpl() {
	if (state == State::Paused)
		state = State::Running;
}

uint32_t Event::Length() const {
	const uint32_t settled = duration.CurrentState();
	if (settled != (uint32_t) -1) return settled;
	// Never put on the clock, so the roll is still open. Everything that starts an event settles
	// it first (see StartImpl/BeginCountdown), so this only happens for one nothing has started.
	return Game::Get()->MutableEvent(key)->duration.Value();
}

void Event::IncrementTimer() {
	// Nothing queued, not just started, and not on any clock: every branch below is a no-op for
	// such an event, down to the `justStarted = false` at the end, so say so once here instead.
	if (TickWouldDoNothing()) return;
	// Anything a task asked of us since our last tick happens now, ahead of everything else.
	if (nextCommand != Command::None) {
		// Cleared first: these run the same code a task would reach were it running inside the
		// event loop, and that code may well queue something new for our next tick.
		Command cmd = nextCommand;
		nextCommand = Command::None;
		// The child-task suppression memory lasts only until the queued command is applied, exactly
		// as ADRIFT clears sTriggeringTask here in IncrementTimer.
		triggeringTask.clear();
		switch (cmd) {
			case Command::Start:  StartImpl(false); break;
			case Command::Stop:   StopImpl(false); break;
			case Command::Pause:  PauseImpl(); break;
			case Command::Resume: ResumeImpl(); break;
			case Command::None:   break;
		}
	}
	// Two steps rather than one, as in ADRIFT: moving the clock can itself change our state, run
	// subevents and restart us, so which state we were in has to be settled beforehand.
	switch (state) {
		case State::CountingDownToStart:
			SetTimeSinceStart(timeSinceStart + 1);
			break;
		case State::Running:
			if (!justStarted) SetTimeSinceStart(timeSinceStart + 1);
			break;
		case State::NotYetStarted:
		case State::Paused:
		case State::Finished:
			break;
	}
	// Re-reads justStarted rather than trusting the value from the top: the clock move above may
	// have restarted us, and a restart has already run its own 0-turn subevents.
	if (!justStarted) DoAnySubEvents();
	justStarted = false;
}

void Event::DoAnySubEvents() {
	if (state != State::Running) return;
	for (int32_t i = 0; i < (int32_t) subevents.size(); i++) {
		auto &se = subevents[i];
		// A subevent counted in seconds only rides along with the event's own clock on a
		// real-time event; on a turn-based one it runs off its own clock instead, driven by
		// StartRealTimeSubEvents/TickRealTimeSubEvents rather than by this loop.
		if (!(se.timeType == TimeType::Turns || timeType == TimeType::RealTime)) continue;
		// Read afresh each time round rather than hoisted out of the loop: an earlier subevent
		// can run a task that reaches back and restarts this very event -- and immediately, since
		// we are inside the event loop -- moving the clock and re-rolling the ranges underneath
		// us. ADRIFT reads them as properties every time for the same reason.
		const int32_t elapsed = timeSinceStart;
		const int32_t len = (int32_t) duration.CurrentState();
		const int32_t when = (int32_t) se.when.Value();
		bool run = false;
		switch (se.whenRefType) {
			case SERefType::EventBegin:
				run = elapsed == when && when <= len
					// "0 turns from the start" is held back on the first run of an event that
					// started immediately, and only then -- see the start-type rewrite in
					// StartImpl, which is what lets it fire on every run after that.
					&& (when > 0 || startType != StartType::Immediately);
				break;
			case SERefType::LastSubEvent:
				// A strict chain: this subevent only ever follows the one before it in the list.
				// If anything else ran last the chain is broken and this one sits the run out.
				// (The first subevent has nothing to follow, so it may open a chain itself.)
				run = TimeSinceLastSubEvent() == when
					&& ((lastSubEventIndex < 0 && i == 0) || (i > 0 && lastSubEventIndex == i - 1));
				break;
			case SERefType::EventEnd:
				run = TimeToEnd() == when;
				break;
		}
		if (run) RunSubEvent(i);
	}
}

void Event::RunSubEvent(int32_t idx) {
	auto &se = subevents[idx];
	auto *g = Game::Get();
	EVENT_TRACE("subevent " << idx << ' ' << magic_enum::enum_name(se.actionType) << ' '
	                        << (se.actionType == SEType::ExecuteTask || se.actionType == SEType::UnsetTask
	                            ? se.actionTask : se.onlyAtLocation));
	switch (se.actionType) {
		case SEType::DisplayMessage:
			// An empty key means the message never shows at all, rather than showing everywhere:
			// ADRIFT tests for a key before it tests where the player is, and so must we.
			if (!se.onlyAtLocation.empty() && g->PlayerIsInLocationOrGroup(se.onlyAtLocation))
				g->OutputFiltered(g->MutableDescription(se.actionDescr)->BuildAndCommit());
			break;
		case SEType::ExecuteTask:
			// Already does nothing for a task that doesn't exist, which is ADRIFT's behaviour
			// here too. A task that fails its restrictions still gets to say so -- ADRIFT only
			// suppresses the hunt for a lower-priority task when an event runs one, not output.
			g->ExecuteTaskByKey(se.actionTask);
			break;
		case SEType::UnsetTask:
			// ADRIFT looks the task up unguarded and throws outright on a key that isn't there;
			// doing nothing is the more useful reading of a game file naming a task it hasn't got.
			if (Task *t = g->GetTask(se.actionTask))
				t->Uncomplete();
			break;
		case SEType::SetLook:
			// Push this subevent's built text onto our look-override stack, exactly as ADRIFT's
			// own RunSubEvent pushes a clsLookText: the text is resolved right now, not lazily
			// when LOOK later reads it back, and an empty onlyAtLocation is pushed too -- it will
			// simply never match anywhere once LookOverrideText goes looking (mirrors ADRIFT,
			// which pushes unconditionally and only tests the key when the stack is read).
			lookOverrides.emplace_back(se.onlyAtLocation, g->MutableDescription(se.actionDescr)->BuildAndCommit());
			break;
	}
	lastSubEventTime = timeSinceStart;
	lastSubEventIndex = idx;
	// A seconds-measured subevent chained onto the one that just ran needs its own clock started
	// now: DoAnySubEvents only drives turn-measured subevents, so nothing else will ever start it.
	// EventBegin- and the chain's own first subevent already had their clocks started back in
	// StartRealTimeSubEvents; this is what carries the chain past that point.
	if (idx + 1 < (int32_t) subevents.size()) {
		auto &next = subevents[idx + 1];
		if (timeType == TimeType::Turns && next.timeType == TimeType::RealTime &&
				next.whenRefType == SERefType::LastSubEvent)
			BeginSubEventCountdown(idx + 1);
	}
}

std::string Event::LookOverrideText() const {
	if (state != State::Running) return "";
	// Most recent first, mirroring ADRIFT reading its stack via ToArray (LIFO order) and taking
	// the first entry whose place the player is in.
	for (auto it = lookOverrides.rbegin(); it != lookOverrides.rend(); ++it)
		if (Game::Get()->PlayerIsInLocationOrGroup(it->first))
			return it->second;
	return "";
}

void Event::StartRealTimeSubEvents() {
	for (int32_t i = 0; i < (int32_t) subevents.size(); i++) {
		auto &se = subevents[i];
		se.secondsRemaining = -1;
		if (timeType != TimeType::Turns || se.timeType != TimeType::RealTime) continue;
		// "N seconds before the end" has no fixed answer when the event's own length is counted
		// in turns, whose real-time length isn't defined -- so this combination, like the
		// equivalent one ADRIFT leaves out of its own start-up loop, never gets a clock.
		if (se.whenRefType == SERefType::EventEnd) continue;
		// A FromLastSubEvent subevent past the first only starts once the one before it in the
		// list has run -- see the chaining logic at the end of RunSubEvent.
		if (se.whenRefType == SERefType::LastSubEvent && i != 0) continue;
		BeginSubEventCountdown(i);
	}
}

void Event::BeginSubEventCountdown(int32_t idx) {
	auto &se = subevents[idx];
	int32_t delay = (int32_t) se.when.Value();
	if (delay <= 0) RunSubEvent(idx);
	else se.secondsRemaining = delay;
}

void Event::TickRealTimeSubEvents() {
	if (state != State::Running) return;
	for (int32_t i = 0; i < (int32_t) subevents.size(); i++) {
		// Read fresh each time round: running a subevent can run a task that restarts this very
		// event, re-rolling every clock here out from under the rest of this loop -- same reason
		// DoAnySubEvents rereads its own state on every iteration.
		auto &se = subevents[i];
		if (se.secondsRemaining < 0) continue;
		if (--se.secondsRemaining <= 0) {
			// Settled before RunSubEvent, not after: leaving it at 0 (or below) would read as
			// still counting down and fire this same subevent again on the very next tick.
			se.secondsRemaining = -1;
			RunSubEvent(i);
		}
	}
}

bool Event::HasRealTimeSubEvents() const {
	for (const auto &se : subevents)
		if (se.timeType == TimeType::RealTime) return true;
	return false;
}

void Event::ReceiveTaskNotification(Util::Control::Condition cond, const std::string &taskKey) {
	// ADRIFT tracks the last task to trigger us this cycle and, on a *completion* control, ignores
	// the trigger when that last task is one of this task's own Specific children -- so a child task
	// (which completes first, deep in the cascade) claims the trigger and the parent's identical
	// control is skipped rather than re-firing it. Only completion controls carry this guard in
	// ADRIFT; uncompletion controls are unaffected.
	if (cond == Util::Control::Condition::Completion && !triggeringTask.empty() &&
			Game::Get()->TaskIsSpecificChildOf(triggeringTask, taskKey))
		return;

	bool fired = false;
	// Every matching control acts, as in ADRIFT: an event that lists the same task twice under
	// the same condition genuinely does the thing twice. (The old code took only the first match
	// -- and, having no end-iterator check, dereferenced controls.cend() outright when a task it
	// had been told about matched no control at all.)
	for (const auto &c : controls) {
		if (c.condition != cond || c.taskName != taskKey) continue;
		switch (c.action) {
			case Util::Control::Action::Start:
				Start();
				break;
			case Util::Control::Action::Stop:
				Stop();
				break;
			case Util::Control::Action::Pause:
				Pause();
				break;
			case Util::Control::Action::Resume:
				Resume();
				break;
		}
		fired = true;
	}
	if (fired && cond == Util::Control::Condition::Completion)
		triggeringTask = taskKey;
}

void Event::WriteState(Save::Writer &writer) const {
	writer.WriteKV("determined_duration", duration.CurrentState());
	writer.WriteKV("determined_start_delay", startDelay.CurrentState());
	// Rewritten as the event runs (StartImpl turns Immediately into AfterDelay for good), so it
	// is state rather than content, however much it looks like the latter. Leave it out and a
	// restored save resurrects Immediately, silently killing every 0-turn subevent the event has.
	writer.WriteKV("start_type", magic_enum::enum_name(startType));
	writer.WriteKV("state", magic_enum::enum_name(state));
	writer.WriteKV("time_since_start", timeSinceStart);
	writer.WriteKV("just_started", justStarted);
	writer.WriteKV("next_command", magic_enum::enum_name(nextCommand));
	writer.WriteKV("triggering_task", triggeringTask);
	writer.WriteKV("last_subevent", lastSubEventIndex);
	writer.WriteKV("last_subevent_time", lastSubEventTime);
	// Each subevent's own settled roll. Every "When" in the test games is a bare number, so these
	// are all foregone conclusions today -- but "3 to 7 FromStartOfEvent" is legal, and then the
	// roll is real state that a restore has no way to guess.
	std::vector<uint32_t> whens;
	whens.reserve(subevents.size());
	for (const auto &se : subevents)
		whens.push_back(se.when.CurrentState());
	writer.WriteKV("subevent_whens", whens);
	// A seconds-measured subevent's own private clock, for the one case it can be mid-countdown
	// on: a turn-based event, whose own tick a save doesn't happen to land on.
	std::vector<int32_t> secondsRemaining;
	secondsRemaining.reserve(subevents.size());
	for (const auto &se : subevents)
		secondsRemaining.push_back(se.secondsRemaining);
	writer.WriteKV("subevent_seconds_remaining", secondsRemaining);
	// The look-override stack, as two parallel lists -- see lookOverrides' declaration.
	std::vector<std::string> lookOverrideKeys, lookOverrideTexts;
	lookOverrideKeys.reserve(lookOverrides.size());
	lookOverrideTexts.reserve(lookOverrides.size());
	for (const auto &lo : lookOverrides) {
		lookOverrideKeys.push_back(lo.first);
		lookOverrideTexts.push_back(lo.second);
	}
	writer.WriteKV("look_override_keys", lookOverrideKeys);
	writer.WriteKV("look_override_texts", lookOverrideTexts);
}

namespace {
// Look a field up by name and check its type. The whole of this used to walk `nextSibling` from
// one named lookup instead, which quietly depended on WriteState's emission order and was already
// wrong once (it read the duration off the event's own compound rather than the field).
const Save::AstNode *GetField(const Save::AstNode *node, const char *name, Save::NodeType type) {
	const auto *f = node->FindChildByName(name);
	return (f && f->type == type) ? f : nullptr;
}
}  // anonymous namespace

bool Event::RestoreState(const Save::AstNode *node) {
	const auto *n = GetField(node, "determined_duration", Save::NT_INT);
	if (!n) return false;
	duration.RestoreState(n->sv.Int);
	if (!(n = GetField(node, "determined_start_delay", Save::NT_INT))) return false;
	startDelay.RestoreState(n->sv.Int);

	if (!(n = GetField(node, "start_type", Save::NT_STRING))) return false;
	auto tmpStartType = magic_enum::enum_cast<StartType>(n->Str);
	if (!tmpStartType.has_value()) return false;
	startType = tmpStartType.value();

	if (!(n = GetField(node, "state", Save::NT_STRING))) return false;
	auto tmpState = magic_enum::enum_cast<State>(n->Str);
	if (!tmpState.has_value()) return false;
	state = tmpState.value();

	if (!(n = GetField(node, "next_command", Save::NT_STRING))) return false;
	auto tmpCmd = magic_enum::enum_cast<Command>(n->Str);
	if (!tmpCmd.has_value()) return false;
	nextCommand = tmpCmd.value();

	if (!(n = GetField(node, "triggering_task", Save::NT_STRING)) &&
			!(n = GetField(node, "triggering_task", Save::NT_EMPTY)))
		return false;
	triggeringTask = n->type == Save::NT_STRING ? n->Str : "";

	if (!(n = GetField(node, "time_since_start", Save::NT_INT))) return false;
	timeSinceStart = (int32_t) n->sv.Int;
	if (!(n = GetField(node, "just_started", Save::NT_BOOL))) return false;
	justStarted = n->sv.Bool;
	if (!(n = GetField(node, "last_subevent", Save::NT_INT))) return false;
	lastSubEventIndex = (int32_t) n->sv.Int;
	if (!(n = GetField(node, "last_subevent_time", Save::NT_INT))) return false;
	lastSubEventTime = (int32_t) n->sv.Int;

	if (!(n = GetField(node, "subevent_whens", Save::NT_INTLIST)) &&
			!(n = GetField(node, "subevent_whens", Save::NT_EMPTY)))
		return false;
	size_t i = 0;
	ITERATE_CHILDREN(n, w) {
		// A different number of subevents than we have means this file isn't describing this
		// game, whatever its header claimed.
		if (i >= subevents.size()) return false;
		subevents[i++].when.RestoreState((uint32_t) w->sv.Int);
	}
	if (i != subevents.size()) return false;

	if (!(n = GetField(node, "subevent_seconds_remaining", Save::NT_INTLIST)) &&
			!(n = GetField(node, "subevent_seconds_remaining", Save::NT_EMPTY)))
		return false;
	i = 0;
	ITERATE_CHILDREN(n, s) {
		if (i >= subevents.size()) return false;
		subevents[i++].secondsRemaining = (int32_t) s->sv.Int;
	}
	if (i != subevents.size()) return false;

	if (!(n = GetField(node, "look_override_keys", Save::NT_STRINGLIST)) &&
			!(n = GetField(node, "look_override_keys", Save::NT_EMPTY)))
		return false;
	std::vector<std::string> lookOverrideKeys;
	ITERATE_CHILDREN(n, k) lookOverrideKeys.push_back(k->Str);
	if (!(n = GetField(node, "look_override_texts", Save::NT_STRINGLIST)) &&
			!(n = GetField(node, "look_override_texts", Save::NT_EMPTY)))
		return false;
	std::vector<std::string> lookOverrideTexts;
	ITERATE_CHILDREN(n, t) lookOverrideTexts.push_back(t->Str);
	if (lookOverrideKeys.size() != lookOverrideTexts.size()) return false;
	lookOverrides.clear();
	for (size_t j = 0; j < lookOverrideKeys.size(); j++)
		lookOverrides.emplace_back(lookOverrideKeys[j], lookOverrideTexts[j]);

	return true;
}

}
