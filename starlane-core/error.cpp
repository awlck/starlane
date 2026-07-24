#include "error.h"

#include <cstdio>
#include <iostream>

// Backtrace capture is inherently platform-specific: there is no portable
// std::stacktrace before C++23, and we target C++17.
#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <cstdlib>
#include <execinfo.h>
#define SLC_HAVE_EXECINFO 1
#endif

namespace Starlane {

std::string CaptureBacktrace() {
#if defined(SLC_HAVE_EXECINFO)
	constexpr int kMaxFrames = 64;
	void *frames[kMaxFrames];
	int n = ::backtrace(frames, kMaxFrames);
	if (n <= 0)
		return {};
	char **symbols = ::backtrace_symbols(frames, n);
	if (!symbols)
		return {};
	std::string result;
	// Skip frame 0 (CaptureBacktrace itself); it is never interesting.
	for (int i = 1; i < n; i++) {
		result += "  ";
		result += symbols[i];
		result += '\n';
	}
	::free(symbols);
	return result;
#elif defined(_WIN32)
	constexpr int kMaxFrames = 64;
	void *frames[kMaxFrames];
	USHORT n = ::CaptureStackBackTrace(1, kMaxFrames, frames, nullptr);
	if (n == 0)
		return {};
	HANDLE process = ::GetCurrentProcess();
	// SymInitialize may already have run elsewhere; a second call is harmless
	// and we do not tear it down, since traces may be captured repeatedly.
	static bool symInitialized = ::SymInitialize(process, nullptr, TRUE);
	std::string result;
	// SYMBOL_INFO is variable-length; back it with a buffer sized for the name.
	alignas(SYMBOL_INFO) char symbolBuf[sizeof(SYMBOL_INFO) + 256];
	auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuf);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = 255;
	for (USHORT i = 0; i < n; i++) {
		DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
		result += "  ";
		if (symInitialized && ::SymFromAddr(process, addr, nullptr, symbol)) {
			result += symbol->Name;
		} else {
			char hex[32];
			std::snprintf(hex, sizeof(hex), "0x%llx", (unsigned long long) addr);
			result += hex;
		}
		result += '\n';
	}
	return result;
#else
	return {};
#endif
}

Exception::Exception(const std::string &what)
	: std::runtime_error(what), backtrace(CaptureBacktrace()) {}

MissingObjectException::MissingObjectException(const std::string &key)
	: Exception("Reference to nonexistent object with key: " + key), key(key) {}

namespace Expr {
GeneralSyntaxException::GeneralSyntaxException(const std::string &expr)
	: Exception("General syntax error parsing expression: \"" + expr + '"'), expr(expr) {}
}

void LogError(const std::string &message) {
	std::cerr << "[starlane] " << message << std::endl;
}

}  // namespace Starlane
