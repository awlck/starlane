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

// Ordinary C++ value semantics: the string is passed and returned by value (copied/moved), so
// there is no shared pointer ownership to manage on either side.
using StringChanger = std::string (*)(const std::string &);
// `text` is owned by the caller (starlane-core) and is only valid for the duration of the call;
// the frontend must not retain or free it.
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

	// put the given yes/no question to the player and return their answer, blocking until
	// they have given one. A frontend that cannot ask must answer `false`.
	// `question` is owned by the caller and only valid for the duration of the call.
	bool (*AskYesNo)(const char *question);
	// the player has quit; tear down the game and, for a frontend that only ever hosts the
	// one game, likely the application along with it. `GameIsOngoing()` already returns
	// false by the time this is called.
	void (*QuitGame)();
	// Give the frontend a chance to service its own event loop (e.g. redraw, handle window/input
	// events) so the application doesn't appear to hang while the engine is busy running many
	// turns or events in a row without otherwise returning control to it -- game processing all
	// happens on the frontend's main thread. Modeled on Glk's own glk_tick(), including its
	// calling convention: the engine calls this liberally, at every iteration of a loop that
	// might otherwise run for a while (see its call sites for exactly where), the same way a Glk
	// library expects glk_tick() to be called on every opcode of a bytecode interpreter. Actually
	// servicing an event loop is not free, though, so a frontend whose event loop is expensive to
	// pump (unlike a batch/console tool, which can leave this a no-op no matter how often it's
	// called) is expected to rate-limit the real work internally -- e.g. only actually pump once
	// some minimum interval has passed -- rather than assume the engine calls this sparingly.
	void (*PumpEvents)();

	// prompt the user to create (or replace) a save file, open it for writing, and return a handle to it
	// The returned handle is an opaque value owned by the frontend; starlane-core never frees it
	// directly, only passing it back to ReadFile/WriteFile/CloseFile. CloseFile is always
	// eventually called to release it.
	void *(*CreateSaveFile)();
	// prompt the user to choose an existing save file to restore from, open it for reading, and return a handle to it
	// (Handle ownership is the same as for CreateSaveFile.)
	void *(*OpenSaveFile)();
	// read up to `bufsize` bytes from `handle` into `buffer`, returning the number of bytes actually read
	// `buffer` is allocated by the caller (starlane-core) with capacity `bufsize`; the frontend
	// fills it but does not own it and must not free it.
	size_t (*ReadFile)(void *handle, uint8_t *buffer, size_t bufsize);
	// write `count` bytes from `buffer` to the file represented by `handle`
	// (A write of length zero is valid and must result in a no-op.)
	// `buffer` is owned by the caller (starlane-core) and only valid for the duration of the call;
	// the frontend must not retain or free it.
	void (*WriteFile)(void *handle, const uint8_t *buffer, size_t count);
	// close the file associated with the given handle, releasing whatever resources the frontend
	// associated with it in CreateSaveFile/OpenSaveFile. The handle is invalid for any further use
	// afterwards.
	void (*CloseFile)(void *handle);
};

// Initialize the backend with the given settings.
// (The argument pointer itself is stowed away, don't delete it.)
SLC_API void InitBackend(const Frontend *settings);

// Debug/trace events: diagnostic messages about what the engine is doing internally -- which task
// matched a command and why, when an event or character walk fires, when a variable changes, and
// so on -- each tagged with a category so a frontend can offer the player (or, just as often, the
// game's author) a way to filter which ones they care about. This is meant to help understand how
// a *game* runs through the engine, not just to debug starlane-core itself, so it is an ordinary
// (if niche) feature available in release builds, not a debug-build-only trace.
//
// No debug event is ever produced unless a callback is registered *and* the event's category is
// currently enabled -- both start out with no effect (no callback, no category enabled) -- so a
// frontend that isn't currently showing any of this pays essentially nothing for it: checking
// whether a category is enabled is cheap enough to happen before a message is even built.
enum class DebugCategory : uint32_t {
	TaskMatching,    // matching player input against tasks' command patterns
	ObjectMatching,  // resolving referenced text to game objects/characters
	TaskSelection,   // choosing among General/Specific tasks once a command has matched
	Restrictions,    // evaluating a task's/description's restriction (pre/postcondition) block
	TaskExecution,   // execution of task actions
	Events,          // the event scheduling/execution cycle
	Walks,           // character walk scheduling/execution
	Variables,       // variable reads/writes
	GameLoad,        // loading and preparing a game file
	InternalErrors,  // messages and stack traces from internal errors
	Miscellaneous,   // miscellaneous errors, e.g. references to unknown objects that don't fit any other contexts
	NumberOfCategories  // dummy value to get the number of categories defined
};
// Number of categories defined above, for a frontend that wants to enumerate/enable all of them
// (e.g. `for (uint32_t i = 0; i < kDebugCategoryCount; i++) ...`) without hardcoding that count or
// depending on which enumerator happens to be listed last.
constexpr uint32_t kDebugCategoryCount = (uint32_t) DebugCategory::NumberOfCategories;

// `message` is owned by the caller (starlane-core) and only valid for the duration of the call.
using DebugEventOutputter = void (*)(DebugCategory category, const char *message);

// Register the callback to receive debug events, or nullptr (the default) to stop receiving them.
SLC_API void SetDebugEventCallback(DebugEventOutputter callback);
// Enable or disable a single category of debug event. Every category starts out disabled; enabling
// one only affects events from this call forward, not any that were skipped before it.
SLC_API void SetDebugEventCategoryEnabled(DebugCategory category, bool enabled);
// Whether the given category is currently enabled.
SLC_API bool IsDebugEventCategoryEnabled(DebugCategory category);
// A short, human-readable name for the category (e.g. "Task Matching"), for a frontend that wants
// to list categories in a UI without hardcoding its own copy of the names.
SLC_API const char *DebugCategoryName(DebugCategory category);

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
// `input` is only needed for the duration of this call and remains owned by the caller. The
// result is returned by value, so the caller owns it as with any other std::string.
SLC_API std::string ExtractTaf(const uint8_t *input, size_t size);
// Whether the frontend should keep reading player input. Stays true once the game has ended (a
// Win/Lose/Neutral ending, or a task's "end the game" action) -- the player can still answer the
// resulting question with RESTART, RESTORE, QUIT, or UNDO, and ProcessInput accepts exactly those.
// Only goes false once the player actually confirms QUIT.
SLC_API bool GameIsOngoing();
// Get blorb resource ID for a given file path, or (uint32_t) -1 if not found.
SLC_API uint32_t GetBlorbResourceForPath(const std::string &path);

// Initiate a save programmatically, e.g. via a menu option:
SLC_API bool SaveGame();
SLC_API bool RestoreGame();

struct SLC_API StatusBar {
	std::string location;
	std::string userStatus;
	int32_t score;
	bool scoringUsed;
};
// Get the current status bar.
// Call this after every time you call Begin(), ProcessInput(), or TimeTick().
// `statusBar` is caller-owned (e.g. stack-allocated) and filled in by this call; its string
// members are ordinary std::strings, so the caller owns them via normal C++ RAII -- no
// explicit release is needed.
SLC_API bool GetStatusBar(StatusBar &statusBar);

// Bibliographic and display info about the current game, as read from the game file itself.
struct SLC_API GameInfo {
	std::string title;
	std::string author;
	// The author's preferred display font (<FontName>), or empty if unspecified.
	std::string fontName;
	// The author's preferred input/output text colors (<InputColour>/<OutputColour>), packed as
	// 0xRRGGBB. hasInputColour/hasOutputColour are false (and the color left at 0) if the game
	// does not specify one -- a frontend should fall back to its own default in that case, rather
	// than treating an absent color as black.
	bool hasInputColour = false;
	uint32_t inputColour = 0;
	bool hasOutputColour = false;
	uint32_t outputColour = 0;
};
// Get bibliographic/display info about the current game. Call any time after CreateGame().
// `info` is caller-owned (e.g. stack-allocated) and filled in by this call; its string members
// are ordinary std::strings, so the caller owns them via normal C++ RAII -- no explicit release
// is needed.
SLC_API bool GetGameInfo(GameInfo &info);
}


#endif  // !SLC_STARLANE_CORE_H
