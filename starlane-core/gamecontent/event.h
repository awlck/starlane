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


private:
	Event() = default;
	std::string key;
	std::vector<std::string> controls;
	StartType startType;
	TimeType timeType;
	Util::Range duration;
	bool repeating;
};

}

#endif  // !SLC_EVENT_H