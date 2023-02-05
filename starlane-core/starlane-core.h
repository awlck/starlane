#pragma once

#ifndef SLC_STARLANE_CORE_H
#define SLC_STARLANE_CORE_H

#include <stdint.h>
#include <string>

#ifdef starlane_core_EXPORTS
#  ifdef _WIN32
#    define SLC_API __declspec(dllexport)
#  else
#    define SLC_API __attribute__((visibility("default")))
#  endif
#else
#  if defined(_WIN32) && defined(SL_SHARED_CORE)
#    define SLC_API __declspec(dllimport)
#  else
#    define SLC_API
#  endif
#endif

namespace Starlane {

using StringChanger = std::string (*)(const std::string &);
using TextOutputter = void (*)(const char *);

// Frontend capabilities and settings
struct SLC_API Frontend {
	// Seed for the random number generator, or zero for a random seed.
	uint32_t randomSeed = 0;

	// Interface the frontend needs to implement
	TextOutputter FatalError;
	TextOutputter OutputText;
	StringChanger StrToUpperCase;
	StringChanger StrToLowerCase;
	StringChanger StrToSentenceCase;
};

// Initialize the backend with the given settings.
SLC_API void InitBackend(const Frontend *settings);
// Load the given 'taf' file content and set up a new game.
// The current game, if any, is discarded. It is your job to ask the user if they are okay with this.
// You may free `tafBytes` immediately once this function returns.
// (Do not pass 'blorb' file data here. If the user requests that a 'blorb' file
//  be loaded, you must first extract the executable chunk.)
SLC_API void CreateGame(const uint8_t *tafBytes, size_t tafLength);
// Perform last-minute data fixups and output the initial batch of text.
SLC_API void BeginGame();
// Call this once per second to advance real-time events.
SLC_API void TimeTick();

// If you just need the unobfuscated XML representation of an ADRIFT game file,
// this function produces it.
SLC_API std::string ExtractTaf(const uint8_t *input, size_t size);
}


#endif  // !SLC_STARLANE_CORE_H