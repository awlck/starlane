#include "walk.h"

#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <magic_enum.hpp>
#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"
#include "../savefiles/writer.h"
#include "../savefiles/parser.h"
#include "character.h"
#include "description.h"
#include "group.h"
#include "location.h"
#include "task.h"

// A walk's schedule is, like an event's, otherwise almost impossible to observe: most of what a walk
// does is move a character or run a task, and neither says which walk did it or when. Mirrors the
// tracing event.cpp keeps for the same reason.
#ifndef NDEBUG
#include <iostream>
#define WALK_TRACE(what) \
	std::cerr << "[walk] " << ownerKey << " '" << description << "' " << what \
	          << " t=" << TimerFromStartOfWalk() << '/' << Length() << '\n'
#else
#define WALK_TRACE(what) ((void) 0)
#endif

namespace Starlane {

Walk Walk::CreateFromXML(const pugi::xml_node &walkNode, const std::string &ownerKey) {
	Walk w;
	w.ownerKey = ownerKey;
	w.description = walkNode.child_value("Description");
	w.loops = ParseBool(walkNode.child_value("Loops"));
	w.startActive = ParseBool(walkNode.child_value("StartActive"));

	// "<Step>Location76 10</Step>", or "<Step>Location79 5 to 12</Step>": the location key is the
	// first token, and whatever follows is the dwell time (a bare number, or a "from to" range).
	for (const auto &it : walkNode.children("Step")) {
		Step s;
		std::string txt = it.child_value();
		auto sp = txt.find(' ');
		if (sp == std::string::npos) {
			s.location = txt;
			s.turns = Util::Range((uint32_t) 0);
		} else {
			s.location = txt.substr(0, sp);
			s.turns = Util::Range(txt.c_str() + sp + 1);
		}
		w.steps.emplace_back(std::move(s));
	}

	// "<Control>Start Completion Task992</Control>" -- parsed exactly as an event's, word by word.
	for (const auto &it : walkNode.children("Control")) {
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
		} else throw std::runtime_error(std::string("Invalid walk control action text: ") + ctrlTxt);
		const char *y;
		if ((y = SkipText(x, "Completion "))) {
			c.condition = Util::Control::Condition::Completion;
		} else if ((y = SkipText(x, "UnCompletion "))) {
			// ADRIFT's own spelling, capital C and all -- it round-trips the enum name verbatim, and
			// jacaranda-jim has a "Stop UnCompletion Task119" walk control that proves the point.
			c.condition = Util::Control::Condition::Uncompletion;
		} else throw std::runtime_error(std::string("Invalid walk control condition text: ") + ctrlTxt);
		c.taskName = y;
		w.controls.emplace_back(c);
	}

	// "<Activity>" -- one sub-walk: a message shown, or a task run, at some point along the walk.
	for (const auto &it : walkNode.children("Activity")) {
		SubWalk sw;
		std::string whenTxt = it.child_value("When");
		if (const char *rest = SkipText(whenTxt.c_str(), "ComesAcross ")) {
			// "ComesAcross <objKey>": fires on meeting, so it carries no turn count.
			sw.when = SubWhen::ComesAcross;
			sw.comesAcrossKey = rest;
		} else {
			// "3 FromStartOfWalk", or "3 to 7 FromStartOfWalk": the reference word is the last token,
			// and everything before it is the (possibly ranged) turn count.
			auto sp = whenTxt.find_last_of(' ');
			if (sp == std::string::npos)
				throw std::runtime_error("Malformed sub-walk When: " + whenTxt);
			sw.turns = Util::Range(whenTxt.substr(0, sp).c_str());
			sw.when = ParseSubWhen(whenTxt.c_str() + sp + 1);
		}
		// Unlike an event's sub-event, a walk's action carries no <What>: a message is an <Action>
		// wrapping a <Description>, and anything else is "ExecuteTask X" / "UnsetTask X" as text. A
		// sub-walk may also carry no <Action> at all (i-summon-thee has one) -- ADRIFT then leaves it
		// a display-message with an empty description, i.e. a sub-walk that does nothing but keep the
		// FromLastSubWalk chain's timing intact. `what` already defaults to DisplayMessage for that.
		const auto &actionNode = it.child("Action");
		if (actionNode.type() != pugi::node_null) {
			if (actionNode.child("Description").type() != pugi::node_null) {
				sw.what = SubWhat::DisplayMessage;
				sw.descr = Game::Get()->CreateDescFromXML(actionNode);
			} else {
				const char *actionTxt = actionNode.child_value();
				if (const char *rest = SkipText(actionTxt, "ExecuteTask ")) {
					sw.what = SubWhat::ExecuteTask;
					sw.taskKey = rest;
				} else if (const char *rest2 = SkipText(actionTxt, "UnsetTask ")) {
					sw.what = SubWhat::UnsetTask;
					sw.taskKey = rest2;
				} else throw std::runtime_error(std::string("Invalid sub-walk action text: ") + actionTxt);
			}
		}
		sw.onlyAtLocation = it.child_value("OnlyApplyAt");
		w.subwalks.emplace_back(std::move(sw));
	}

	return w;
}

void Walk::RegisterNotifications(int32_t selfIndex) const {
	// Once per task and condition, however many controls name that pair -- ReceiveTaskNotification
	// walks all of a walk's matching controls itself, so subscribing twice would run each twice.
	std::set<std::pair<std::string, Util::Control::Condition>> subscribed;
	for (const auto &c : controls) {
		if (!subscribed.emplace(c.taskName, c.condition).second) continue;
		if (Task *t = Game::Get()->GetTask(c.taskName))
			t->RegisterWalkNotification(ownerKey, selfIndex, c.condition);
	}
}

Character *Walk::Owner() const {
	// Mutable: a walk exists to move its owner about, so every caller of this changes them.
	return AsCharacter(Game::Get()->MutableObject(ownerKey));
}

void Walk::Start(bool force) {
	if (force) { StartImpl(false); return; }
	// A stop already queued for this turn plus a start becomes a restart, so the pair doesn't cancel
	// out into nothing.
	if (nextCommand == Command::Stop) nextCommand = Command::Restart;
	else nextCommand = Command::Start;
}

void Walk::Stop() { nextCommand = Command::Stop; }
void Walk::Pause() { nextCommand = Command::Pause; }
void Walk::Resume() { nextCommand = Command::Resume; }

void Walk::StartImpl(bool restart) {
	// ADRIFT logs "Can't Start a Walk that isn't waiting!" and does nothing.
	if (!(status == Status::NotYetStarted || status == Status::Finished ||
			(status == Status::Running && restart)))
		return;
	status = Status::Running;
	// A "3 to 7 turns" step rolls afresh at each start rather than being stuck with its first roll.
	ResetLength();
	// Length + 1, so that TimerFromStartOfWalk lands on 0 right now and the very first step (the one
	// at cumulative offset 0) fires from the DoAnySteps below. Length + 1 is never 0, so this write
	// can't trip the end-of-walk stop.
	SetTimerToEnd(Length() + 1);
	WALK_TRACE((restart ? "restart" : "start"));
	DoAnySteps();
	DoAnySubWalks();
}

void Walk::StopImpl(bool runSubWalks, bool reachedEnd) {
	// End-of-walk sub-walks get their turn before the walk is called finished; it is the only chance
	// a "N turns before the end" sub-walk ever gets.
	if (runSubWalks) DoAnySubWalks();
	status = Status::Finished;
	WALK_TRACE("stop");
	// Restart only when the walk actually ran its clock out, not when a task stopped it early --
	// ADRIFT draws that line to avoid a task-stopped walk quietly restarting itself.
	if (loops && reachedEnd)
		StartImpl(true);
}

void Walk::PauseImpl() {
	if (status == Status::Running)
		status = Status::Paused;
}

void Walk::ResumeImpl() {
	if (status == Status::Paused)
		status = Status::Running;
}

void Walk::IncrementTimer() {
	// Anything a task asked of this walk since its last tick happens now, ahead of everything else.
	if (nextCommand != Command::None) {
		Command cmd = nextCommand;
		nextCommand = Command::None;
		// The child-task suppression memory lasts only until the queued command is applied, exactly
		// as ADRIFT clears sTriggeringTask here.
		triggeringTask.clear();
		switch (cmd) {
			case Command::Start:   StartImpl(false); break;
			case Command::Stop:    StopImpl(false, false); break;
			case Command::Pause:   PauseImpl(); break;
			case Command::Resume:  ResumeImpl(); break;
			case Command::Restart: StartImpl(true); break;
			case Command::None:    break;
		}
	}
	// The just-started walk already ran its offset-0 step and sub-walks from within StartImpl, and
	// TimerFromStartOfWalk is 0 for exactly that tick -- so this guard holds them back from running a
	// second time, and lets them run on every tick thereafter.
	if (timerToEnd > 0 && TimerFromStartOfWalk() > 0) {
		DoAnySteps();
		DoAnySubWalks();
	}
	// Split from the block above, as in ADRIFT: moving the clock can stop (and restart) the walk, so
	// which state it is in has to be settled first.
	if (status == Status::Running)
		SetTimerToEnd(timerToEnd - 1);
}

void Walk::SetTimerToEnd(int32_t t) {
	timerToEnd = t;
	// Run out of time: end the walk (and loop it, if set to). The only path a looping walk repeats
	// on, which is what keeps a task-stopped one -- stopped with time still on its clock -- stopped.
	if (status == Status::Running && timerToEnd == 0)
		StopImpl(true, true);
}

int32_t Walk::Length() const {
	int32_t len = 0;
	for (const auto &s : steps)
		len += (int32_t) s.turns.CurrentState();
	return len;
}

void Walk::ResetLength() {
	for (auto &s : steps) {
		s.turns.Reset();
		s.turns.Value();  // settle the roll now, so Length() has a fixed answer for the rest of the run
	}
}

void Walk::DoAnySteps() {
	if (status != Status::Running) return;
	Character *owner = Owner();
	if (!owner) return;
	auto *g = Game::Get();

	int32_t stepLength = 0;
	const int32_t fromStart = TimerFromStartOfWalk();
	for (const auto &step : steps) {
		if (stepLength == fromStart) {
			std::string dest = step.location;
			// "%Player%" resolves to the player's key, which then falls into the follow-a-character
			// branch below -- so a "%Player%" step is "follow the player, if adjacent".
			if (Util::IsReference(dest)) dest = g->GetReference(dest);

			std::string resolved;
			if (g->GroupExists(dest)) {
				// Wander to a member of the group, preferring one adjacent to where the character is.
				const auto &members = Game::Get()->GetGroup(dest)->GetAllMembers();
				const Location *cur = owner->GetLocation();  // null if the character is hidden
				bool hasAdjacent = false;
				if (cur) {
					for (const auto &m : members)
						if (cur->IsAdjacent(m)) { hasAdjacent = true; break; }
				}
				if (hasAdjacent) {
					while (resolved.empty()) {
						const std::string &cand = PickRandomMember(members);
						if (cur->IsAdjacent(cand)) resolved = cand;
					}
				} else if (!members.empty()) {
					resolved = PickRandomMember(members);
				}
			} else if (AsCharacter(g->TryGetObject(dest))) {
				// Follow a character, but only if they are in an adjacent room.
				const std::string &targetLoc = g->GetObject(dest)->GetLocationKey();
				if (owner->GetLocationKey() != targetLoc) {
					const Location *cur = owner->GetLocation();
					if (cur && cur->IsAdjacent(targetLoc)) resolved = targetLoc;
				}
			} else {
				// A literal location key (or "Hidden").
				resolved = dest;
			}

			if (resolved == "Hidden" || AsLocation(g->TryGetObject(resolved))) {
				AnnounceMove(*owner, resolved);
				owner->MoveTo(resolved, resolved == "Hidden" ? GameObj::HoldingType::Hidden
				                                             : GameObj::HoldingType::AtLocation);
			}
		}
		// CurrentState(), not Value(): every step's length was settled in ResetLength at the start of
		// this run, so there is a fixed answer already, and `step` here is a const reference besides.
		stepLength += (int32_t) step.turns.CurrentState();
	}
}

void Walk::AnnounceMove(Character &owner, const std::string &dest) const {
	// ADRIFT narrates a walking character's comings and goings only when it has the ShowEnterExit
	// property and is standing directly at a location (rather than hidden, or on/in something).
	if (!owner.HasProp("ShowEnterExit") ||
			owner.GetParentRelation() != GameObj::HoldingType::AtLocation)
		return;
	auto *g = Game::Get();
	// CharEnters/CharExits are Text properties, which ADRIFT stores as descriptions rather than as
	// plain strings -- so they are built, not read off. (They can also carry restrictions and
	// alternatives like any other description: "slinks in" while sneaking, "marches in" otherwise.)
	auto verbFor = [&](const char *prop, const char *fallback) {
		if (!owner.HasProp(prop)) return std::string(fallback);
		const auto ref = (DescrRef) owner.GetIntProp(prop);
		if (ref == 0) return std::string(fallback);
		std::string text = g->MutableDescription(ref)->BuildAndCommit();
		return text.empty() ? std::string(fallback) : text;
	};
	const std::string ownerLoc = owner.GetLocationKey();
	const std::string &playerLoc = g->GetPlayerLocationKey();
	// The name leads a fresh sentence; OutputFiltered capitalises the first letter for us.
	if (ownerLoc == playerLoc) {
		// The player watches the character leave.
		std::string verb = verbFor("CharExits", "exits");
		std::string msg = owner.GetDisplayName(false) + " " + verb;
		if (const Location *from = owner.GetLocation(); from && from->IsAdjacent(dest)) {
			std::string dir = from->DirectionTo(dest);
			if (dir != "nowhere") {
				// "inside"/"outside" read as bare adverbs; the compass directions want a "to".
				msg += (dir == "inside" || dir == "outside") ? " " : " to ";
				msg += dir;
			}
		}
		msg += ".";
		g->OutputFiltered(msg);
	} else if (dest == playerLoc) {
		// The player watches the character arrive.
		std::string verb = verbFor("CharEnters", "enters");
		std::string msg = owner.GetDisplayName(false) + " " + verb;
		if (const Location *destLoc = AsLocation(g->TryGetObject(dest));
				destLoc && destLoc->IsAdjacent(ownerLoc)) {
			std::string dir = destLoc->DirectionTo(ownerLoc);
			if (dir != "nowhere") msg += " from " + dir;
		}
		msg += ".";
		g->OutputFiltered(msg);
	}
}

const std::string &Walk::PickRandomMember(const std::set<std::string> &members) {
	auto it = members.begin();
	std::advance(it, RandomInt((uint32_t) members.size() - 1));
	return *it;
}

void Walk::DoAnySubWalks() {
	if (status != Status::Running) return;
	Character *owner = Owner();
	if (!owner) return;
	auto *g = Game::Get();

	for (int32_t i = 0; i < (int32_t) subwalks.size(); i++) {
		auto &sw = subwalks[i];
		bool run = false;
		switch (sw.when) {
			case SubWhen::FromStartOfWalk:
				run = TimerFromStartOfWalk() == (int32_t) sw.turns.Value() &&
				      (int32_t) sw.turns.Value() <= Length();
				break;
			case SubWhen::FromLastSubWalk:
				// A strict chain: this sub-walk only ever follows the one before it in the list.
				run = TimerFromLastSubWalk() == (int32_t) sw.turns.Value() &&
				      ((lastSubWalkIndex < 0 && i == 0) || (i > 0 && lastSubWalkIndex == i - 1));
				break;
			case SubWhen::BeforeEndOfWalk:
				run = timerToEnd == (int32_t) sw.turns.Value();
				break;
			case SubWhen::ComesAcross: {
				// Rising edge of "the character is where the player is". ADRIFT ties this to the
				// player regardless of what the sub-walk names, and so do we.
				bool prev = sw.sameLocationAsChar;
				sw.sameLocationAsChar = (owner->GetLocationKey() == g->GetPlayerLocationKey());
				run = !prev && sw.sameLocationAsChar;
				break;
			}
		}
		if (run) RunSubWalk(i);
	}
}

void Walk::RunSubWalk(int32_t idx) {
	auto &sw = subwalks[idx];
	auto *g = Game::Get();
	WALK_TRACE("sub-walk " << idx << ' ' << magic_enum::enum_name(sw.what));
	switch (sw.what) {
		case SubWhat::DisplayMessage:
			// An empty gate means the message never shows, rather than showing everywhere: ADRIFT
			// tests for the key before it tests where the player is. `descr` of 0 is the no-message
			// sub-walk the loader leaves behind for an <Activity> with no <Action> -- nothing to show.
			if (sw.descr != 0 && !sw.onlyAtLocation.empty() && g->PlayerIsInLocationOrGroup(sw.onlyAtLocation))
				g->OutputFiltered(g->MutableDescription(sw.descr)->BuildAndCommit());
			break;
		case SubWhat::ExecuteTask: {
			// Top-level execution, as when an event runs a task -- see Game::ResponseScope.
			Game::ResponseScope scope(g);
			g->ExecuteTaskByKey(sw.taskKey);
			break;
		}
		case SubWhat::UnsetTask:
			if (Task *t = g->GetTask(sw.taskKey))
				t->Uncomplete();
			break;
	}
	lastSubWalkTime = TimerFromStartOfWalk();
	lastSubWalkIndex = idx;
}

void Walk::ReceiveTaskNotification(Util::Control::Condition cond, const std::string &taskKey) {
	// A completion control is ignored when the last task to trigger this walk this cycle is one of
	// the completing task's own Specific children -- ADRIFT's sTriggeringTask guard, so a child task
	// (which completes first) claims the trigger and the parent's identical control does not re-fire.
	// Only completion controls carry the guard in ADRIFT; uncompletion controls are unaffected.
	if (cond == Util::Control::Condition::Completion && !triggeringTask.empty() &&
			Game::Get()->TaskIsSpecificChildOf(triggeringTask, taskKey))
		return;

	bool fired = false;
	// Every matching control acts, as in ADRIFT: a walk that lists the same task twice under the same
	// condition genuinely does the thing twice.
	for (const auto &c : controls) {
		if (c.condition != cond || c.taskName != taskKey) continue;
		switch (c.action) {
			case Util::Control::Action::Start:  Start(); break;
			case Util::Control::Action::Stop:   Stop(); break;
			case Util::Control::Action::Pause:  Pause(); break;
			case Util::Control::Action::Resume: Resume(); break;
		}
		fired = true;
	}
	if (fired && cond == Util::Control::Condition::Completion)
		triggeringTask = taskKey;
}

void Walk::WriteState(Save::Writer &writer) const {
	writer.WriteKV("status", magic_enum::enum_name(status));
	writer.WriteKV("timer_to_end", timerToEnd);
	writer.WriteKV("last_subwalk_time", lastSubWalkTime);
	writer.WriteKV("last_subwalk", lastSubWalkIndex);
	writer.WriteKV("next_command", magic_enum::enum_name(nextCommand));
	writer.WriteKV("triggering_task", triggeringTask);
	// Each step's settled length. All bare numbers in the test games, so foregone conclusions today,
	// but "5 to 12" is legal and then the roll is real state a restore can't otherwise guess.
	std::vector<uint32_t> stepTurns;
	stepTurns.reserve(steps.size());
	for (const auto &s : steps) stepTurns.push_back(s.turns.CurrentState());
	writer.WriteKV("step_turns", stepTurns);
	// The same for each sub-walk's settled "when".
	std::vector<uint32_t> subTurns;
	subTurns.reserve(subwalks.size());
	for (const auto &sw : subwalks) subTurns.push_back(sw.turns.CurrentState());
	writer.WriteKV("subwalk_turns", subTurns);
	// The ComesAcross rising-edge state, so a restore doesn't re-announce a meeting already made.
	std::vector<int32_t> subSameLoc;
	subSameLoc.reserve(subwalks.size());
	for (const auto &sw : subwalks) subSameLoc.push_back(sw.sameLocationAsChar ? 1 : 0);
	writer.WriteKV("subwalk_same_location", subSameLoc);
}

namespace {
const Save::AstNode *GetField(const Save::AstNode *node, const char *name, Save::NodeType type) {
	const auto *f = node->FindChildByName(name);
	return (f && f->type == type) ? f : nullptr;
}
}  // anonymous namespace

bool Walk::RestoreState(const Save::AstNode *node) {
	const auto *n = GetField(node, "status", Save::NT_STRING);
	if (!n) return false;
	auto tmpStatus = magic_enum::enum_cast<Status>(n->Str);
	if (!tmpStatus.has_value()) return false;
	status = tmpStatus.value();

	if (!(n = GetField(node, "next_command", Save::NT_STRING))) return false;
	auto tmpCmd = magic_enum::enum_cast<Command>(n->Str);
	if (!tmpCmd.has_value()) return false;
	nextCommand = tmpCmd.value();

	if (!(n = GetField(node, "triggering_task", Save::NT_STRING)) &&
			!(n = GetField(node, "triggering_task", Save::NT_EMPTY)))
		return false;
	triggeringTask = n->type == Save::NT_STRING ? n->Str : "";

	if (!(n = GetField(node, "timer_to_end", Save::NT_INT))) return false;
	timerToEnd = (int32_t) n->sv.Int;
	if (!(n = GetField(node, "last_subwalk_time", Save::NT_INT))) return false;
	lastSubWalkTime = (int32_t) n->sv.Int;
	if (!(n = GetField(node, "last_subwalk", Save::NT_INT))) return false;
	lastSubWalkIndex = (int32_t) n->sv.Int;

	if (!(n = GetField(node, "step_turns", Save::NT_INTLIST)) &&
			!(n = GetField(node, "step_turns", Save::NT_EMPTY)))
		return false;
	size_t i = 0;
	ITERATE_CHILDREN(n, w) {
		// A different number of steps than we have means this file isn't describing this game.
		if (i >= steps.size()) return false;
		steps[i++].turns.RestoreState((uint32_t) w->sv.Int);
	}
	if (i != steps.size()) return false;

	if (!(n = GetField(node, "subwalk_turns", Save::NT_INTLIST)) &&
			!(n = GetField(node, "subwalk_turns", Save::NT_EMPTY)))
		return false;
	i = 0;
	ITERATE_CHILDREN(n, w) {
		if (i >= subwalks.size()) return false;
		subwalks[i++].turns.RestoreState((uint32_t) w->sv.Int);
	}
	if (i != subwalks.size()) return false;

	if (!(n = GetField(node, "subwalk_same_location", Save::NT_INTLIST)) &&
			!(n = GetField(node, "subwalk_same_location", Save::NT_EMPTY)))
		return false;
	i = 0;
	ITERATE_CHILDREN(n, w) {
		if (i >= subwalks.size()) return false;
		subwalks[i++].sameLocationAsChar = w->sv.Int != 0;
	}
	if (i != subwalks.size()) return false;
	return true;
}

Walk::SubWhen Walk::ParseSubWhen(const char *txt) {
	auto tmp = magic_enum::enum_cast<SubWhen>(txt);
	if (!tmp.has_value())
		throw std::runtime_error(std::string("Invalid sub-walk When reference: ") + txt);
	return tmp.value();
}

}
