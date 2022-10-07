#pragma once

#ifndef SLC_STARLANE_CORE_H
#define SLC_STARLANE_CORE_H

#include <stdint.h>
#include <string>

// Interface the frontend needs to implement
namespace SLFrontend {
// Show a fatal error message. The frontend should refuse any further input
// after a fatal error has been issued.
void FatalError(const char *msg);
// Send text to the output.
void OutputText(const char *txt);

// Utility functions that are easy to implement using Qt or Glk library features,
// but are very hard to do in plain C++, so we ask the frontend to do these things for us.
namespace Services {
// make an uppercase version of the string `s`
std::string StrToUpperCase(const std::string &s);
// make a lowercase version of the string `s`
std::string StrToLowerCase(const std::string &s);
// make a sentence-cased version of the string `s`
std::string StrToSentenceCase(const std::string &s);
}  // namespace Services

}  // namespace SLFrontend


// Interface provided by starlane-core
namespace Starlane {
// Frontend capabilities and settings
struct FECapabilities {
	// Seed for the random number generator, or zero for a random seed.
	uint32_t randomSeed = 0;
};

// Initialize the backend with the given settings.
void InitBackend(const FECapabilities &settings);
// Perform last-minute data fixups and output the initial batch of text.
void BeginGame();
// Call this once per second to advance real-time events.
void TimeTick();
}


#endif  // !SLC_STARLANE_CORE_H