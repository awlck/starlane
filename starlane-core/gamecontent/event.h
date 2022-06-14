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
		TimeBased,
		TaskBased
	};
	static StartType ParseStartType(const char *txt);

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
	};

private:
	Event() = default;
	std::string key;
	std::vector<Util::Control> controls;
	std::vector<Subevent> subevents;
	StartType startType;
	TimeType timeType;
	Util::Range duration;
	bool repeating;
	bool repeatCountdown;
};

}

#endif  // !SLC_EVENT_H