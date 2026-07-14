#include "event.h"

#include <algorithm>

#include <magic_enum.hpp>
#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"
#include "../savefiles/writer.h"
#include "../savefiles/parser.h"

namespace Starlane {

Event *Event::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Event;
	result->key = xmlNode.child_value("Key");
	result->startType = ParseStartType(xmlNode.child_value("WhenStart"));
	result->timeType = ParseTimeType(xmlNode.child_value("Type"));
	result->repeating = ParseBool(xmlNode.child_value("Repeating"));
	result->repeatCountdown = xmlNode.child("RepeatCountdown").type() != pugi::node_null;
	result->duration = Util::Range(xmlNode.child_value("Length"));

	if (result->startType == StartType::TaskBased) {
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
			} else if ((y = SkipText(x, "Uncompletion "))) {
				c.condition = Util::Control::Condition::Uncompletion;
			} else throw std::runtime_error(std::string("Invalid control condition text: ") + ctrlTxt);
			c.taskName = y;
			result->controls.emplace_back(c);
		}
	}

	// TODO: handle other StartTypes?

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

void Event::Start() {
	// TODO
}

void Event::Stop() {
	// TODO
}

void Event::Pause() {
	if (state == State::Running)
		state = State::Paused;
}

void Event::Resume() {
	if (state == State::Paused)
		state = State::Running;
}

void Event::ReceiveTaskNotification(Util::Control::Condition cond, const std::string &taskKey) {
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
	}
	// TODO: ADRIFT additionally ignores a control whose triggering task is a child of the task
	// currently completing, so that a child task can't re-trigger what its parent already did.
	// Not modelled here.
}

void Event::WriteState(Save::Writer &writer) const {
	writer.WriteKV("determined_duration", duration.CurrentState());
	writer.WriteKV("state", magic_enum::enum_name(state));
	writer.WriteKV("time_since_start", timeSinceStart);
}

bool Event::RestoreState(const Save::AstNode *node) {
	const auto *ddNode = node->FindChildByName("determined_duration");
	if (!ddNode || ddNode->type != Save::NT_INT) return false;
	// `ddNode`, not `node`: the latter is the event's own compound, whose `sv` union holds its
	// child list rather than an integer -- so reading `.Int` off it reinterpreted a pointer as
	// the duration, quietly corrupting it on every single restore.
	duration.RestoreState(ddNode->sv.Int);
	const auto *sNode = ddNode->nextSibling;
	if (!sNode || sNode->type != Save::NT_STRING) return false;
	auto tmpState = magic_enum::enum_cast<State>(sNode->Str);
	if (!tmpState.has_value()) return false;
	state = tmpState.value();
	const auto *tssNode = sNode->nextSibling;
	if (!tssNode || tssNode->type != Save::NT_INT) return false;
	timeSinceStart = (int32_t) tssNode->sv.Int;
	return true;
}

}
