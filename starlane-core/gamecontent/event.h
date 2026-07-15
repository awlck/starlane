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
	};

	void ReceiveTaskNotification(Util::Control::Condition ctrl, const std::string &taskKey);
	void Start();
	void Pause();
	void Resume();

	// Whether this event is driven by the wall clock rather than by turns. The only thing that
	// tells the two populations of events apart; both tick through IncrementTimer.
	bool IsRealTime() const { return timeType == TimeType::RealTime; }
	// Advance this event by one tick of whichever clock it runs on.
	void IncrementTimer();
	// Clear the "started on this very tick" flag. Done in a pass of its own once every event has
	// ticked, rather than by the event itself -- see Game::RunEventTick.
	void ClearJustStarted() { justStarted = false; }

	Util::Range &GetDuration() { return duration; }
	int32_t GetTimeSinceStart() const { return timeSinceStart; }

	void WriteState(Save::Writer &writer) const;
	bool RestoreState(const Save::AstNode *node);

private:
	Event() = default;
	void Stop();

	enum class State {
		NotOngoing,
		Running,
		Paused
	};

	std::string key;
	std::vector<Util::Control> controls;
	std::vector<Subevent> subevents;
	StartType startType;
	TimeType timeType;
	Util::Range duration;
	bool repeating;
	bool repeatCountdown;
	State state = State::NotOngoing;
	int32_t timeSinceStart = 0;
	// An event that started on this very tick does not also age on it.
	bool justStarted = false;
};

}

#endif  // !SLC_EVENT_H