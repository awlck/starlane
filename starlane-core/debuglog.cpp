#include "debuglog.h"

namespace Starlane {

namespace {
DebugEventOutputter gCallback = nullptr;
uint32_t gEnabledMask = 0;
}  // namespace

namespace DebugLog {

bool IsEnabled(DebugCategory category) {
	return gCallback != nullptr && (gEnabledMask & (1u << (uint32_t) category)) != 0;
}

void Emit(DebugCategory category, const std::string &message) {
	if (gCallback) gCallback(category, message.c_str());
}

}  // namespace DebugLog

void SetDebugEventCallback(DebugEventOutputter callback) {
	gCallback = callback;
}

void SetDebugEventCategoryEnabled(DebugCategory category, bool enabled) {
	uint32_t bit = 1u << (uint32_t) category;
	gEnabledMask = enabled ? (gEnabledMask | bit) : (gEnabledMask & ~bit);
}

bool IsDebugEventCategoryEnabled(DebugCategory category) {
	return (gEnabledMask & (1u << (uint32_t) category)) != 0;
}

const char *DebugCategoryName(DebugCategory category) {
	switch (category) {
	case DebugCategory::TaskMatching: return "Task Matching";
	case DebugCategory::ObjectMatching: return "Object Matching";
	case DebugCategory::TaskSelection: return "Task Selection";
	case DebugCategory::Restrictions: return "Restrictions";
	case DebugCategory::Events: return "Events";
	case DebugCategory::Walks: return "Walks";
	case DebugCategory::Variables: return "Variables";
	case DebugCategory::GameLoad: return "Game Load";
	case DebugCategory::InternalErrors: return "Internal Error";
	case DebugCategory::Miscellaneous: return "Miscellaneous";
	default: return "<invalid log category>";
	}
	return "Unknown";
}

}  // namespace Starlane
