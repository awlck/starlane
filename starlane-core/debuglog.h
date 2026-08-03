// Internal machinery behind the debug-event API declared in starlane-core.h. Everything here is
// for starlane-core's own call sites; a frontend only ever sees SetDebugEventCallback/
// SetDebugEventCategoryEnabled/IsDebugEventCategoryEnabled/DebugCategoryName over there.
//
// A call site doesn't use DebugLog directly -- it uses the SL_DEBUG macro below, which checks
// DebugLog::IsEnabled *before* evaluating its stream expression, so a category with no listener
// (or that just isn't currently enabled) costs one bool check rather than the cost of building a
// message nobody will see.

#pragma once

#ifndef SLC_DEBUGLOG_H
#define SLC_DEBUGLOG_H

#include <sstream>
#include <string>

#include "starlane-core.h"

namespace Starlane {

namespace DebugLog {

bool IsEnabled(DebugCategory category);
void Emit(DebugCategory category, const std::string &message);

}  // namespace DebugLog

}  // namespace Starlane

// Emit a debug event of the given category (an unqualified DebugCategory enumerator, e.g.
// `TaskMatching`), built from a '<<' stream expression, e.g.:
//   SL_DEBUG(TaskMatching, "trying " << task->Key() << " against \"" << currentCommand << '"');
// `streamExpr` is only evaluated when `category` is enabled, so it may freely call functions or
// format values that would otherwise not be worth the cost of computing on every call.
#define SL_DEBUG(category, streamExpr) \
	do { \
		if (::Starlane::DebugLog::IsEnabled(::Starlane::DebugCategory::category)) { \
			std::ostringstream _sl_debug_msg; \
			_sl_debug_msg << streamExpr; \
			::Starlane::DebugLog::Emit(::Starlane::DebugCategory::category, _sl_debug_msg.str()); \
		} \
	} while (0)

#endif  // !SLC_DEBUGLOG_H
