#include "event.h"

#include <sstream>

#include <pugixml.hpp>

#include "../game.h"
#include "../valueparsers.h"

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
			} else if ((x = SkipText(ctrlTxt, "Pause "))) {
				c.action = Util::Control::Action::Pause;
			} else if ((x = SkipText(ctrlTxt, "Resume "))) {
				c.action = Util::Control::Action::Resume;
			} else throw std::runtime_error(std::string("Invalid event control action text: ") + ctrlTxt);
			if ((x = SkipText(x, "Completion "))) {
				c.condition = Util::Control::Condition::Completion;
			} else if ((x = SkipText(ctrlTxt, "Uncompletion "))) {
				c.condition = Util::Control::Condition::Uncompletion;
			} else throw std::runtime_error(std::string("Invalid control condition text: ") + ctrlTxt);
			c.taskName = x;
			result->controls.emplace_back(c);
		}
	}

	for (const auto &it: xmlNode.children("SubEvent")) {
		Subevent se;
		{  // Parse "<When>1 FromStartOfEvent</When>" -- this is just terrible.
			const char *rangeTxtOrig = it.child_value("When");
			const char *rtypeTxt = rangeTxtOrig;
			while (*rtypeTxt && (isdigit(*rtypeTxt) || *rtypeTxt == ' ' || *rtypeTxt == 't' || *rtypeTxt == 'o'))
				rtypeTxt++;
			ptrdiff_t rangeLen = rtypeTxt - rangeTxtOrig;
			char *rangeTxt = new char[rangeLen + 1];
			strncpy(rangeTxt, rangeTxtOrig, rangeLen);
			se.when = Util::Range(rangeTxt);
			se.whenRefType = ParseSERefType(rtypeTxt);
			delete[] rangeTxt;
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

}