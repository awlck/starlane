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
	uint32_t randomSeed;
	// whether the frontend can support real-time events
	bool timersAvailable;

	// Interface the frontend needs to implement
	TextOutputter FatalError;  // issue a fatal error message
	TextOutputter OutputText;  // send text to the output
	StringChanger StrToUpperCase;  // translate the given string to upper case
	StringChanger StrToLowerCase;  // translate the given string to lower case
	StringChanger StrToSentenceCase;  // translate the given string to sentence case

	// prompt the user to create (or replace) a save file, open it for writing, and return a handle to it
	void *(*CreateSaveFile)();
	// prompt the user to choose an existing save file to restore from, open it for reading, and return a handle to it
	void *(*OpenSaveFile)();
	// read up to `bufsize` bytes from `handle` into `buffer`, returning the number of bytes actually read
	size_t (*ReadFile)(void *handle, uint8_t *buffer, size_t bufsize);
	// write `count` bytes from `buffer` to the file represented by `handle`
	// (A write of length zero is valid and must result in a no-op.)
	void (*WriteFile)(void *handle, const uint8_t *buffer, size_t count);
	void (*CloseFile)(void *handle);  // close the file associated with the given handle
};

// Initialize the backend with the given settings.
// (The argument pointer itself is stowed away, don't delete it.)
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
// Input text for processing:
SLC_API void ProcessInput(const std::string &cmd);

// If you just need the unobfuscated XML representation of an ADRIFT game file,
// this function produces it.
SLC_API std::string ExtractTaf(const uint8_t *input, size_t size);
// Whether a game has been loaded and begun
SLC_API bool GameIsOngoing();
// Get blorb resource ID for a given file path, or (uint32_t) -1 if not found.
SLC_API uint32_t GetBlorbResourceForPath(const std::string &path);

// Initiate a save programmatically, e.g. via a menu option:
SLC_API bool SaveGame();
SLC_API bool RestoreGame();
}


#endif  // !SLC_STARLANE_CORE_H
