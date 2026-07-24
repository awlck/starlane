// Error handling: a common exception base that captures a backtrace at throw
// time, and a stderr-backed logging helper.

#pragma once

#ifndef SLC_ERROR_H
#define SLC_ERROR_H

#include <stdexcept>
#include <string>

namespace Starlane {

// Base for all exceptions Starlane itself throws. The backtrace is captured in
// the constructor -- i.e. at (or very near) the throw site -- because a trace
// gathered later, inside a catch block, would only show the already-unwound
// catch site. Catch this at a boundary (see the API wrappers in
// starlane-core.cpp) and print trace() to a developer-facing log.
class Exception : public std::runtime_error {
public:
	explicit Exception(const std::string &what);

	// Human-readable backtrace captured at construction. Empty on platforms
	// where we cannot capture one.
	const std::string &trace() const { return backtrace; }

private:
	std::string backtrace;
};

// Thrown by Game::GetObject when a key names no object -- either a malformed
// game file or an unset reference evaluated too eagerly. Carries the key so the
// log names it.
class MissingObjectException : public Exception {
public:
	explicit MissingObjectException(const std::string &key);
	const std::string &Key() const { return key; }

private:
	std::string key;
};

// Capture a backtrace of the current call stack as a printable string. Used by
// Exception's constructor; exposed for ad-hoc diagnostics too. Returns an empty
// string where backtracing is unavailable.
std::string CaptureBacktrace();

// Log a developer-facing error. Writes to stderr for now; this is the seam for
// the proper debug log that is still on the to-do list.
// TODO(debug-log): route through a real log sink once one exists.
void LogError(const std::string &message);

}  // namespace Starlane

#endif  // !SLC_ERROR_H
