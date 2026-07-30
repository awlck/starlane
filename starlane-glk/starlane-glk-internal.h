//
// Created by Adrian Welcker on 17.07.26.
//

#pragma once

#ifndef STARLANE_GLK_INTERNAL_H
#define STARLANE_GLK_INTERNAL_H

#include <stdint.h>

#include <string>
#include <vector>

#include "starlane-core.h"

extern "C" {
#include "glkext.h"
}

// Bit flags describing the (best-effort) text style in effect for a run of output text,
// as accumulated from nested ADRIFT-style markup tags. See AppendHtml() in output.cpp.
enum TextStyleFlags : uint32_t {
	kStyleNormal    = 0,
	kStyleBold      = 1u << 0,
	kStyleItalic    = 1u << 1,
	kStyleCentered  = 1u << 2,
	kStyleInput     = 1u << 3,  // `<c>...</c>`: text that reads like a command the player could type
	kStyleMonospace = 1u << 4,  // `<font face="...">` naming one of the known monospace fonts
};

// The main (text buffer) output window and its stream, and the status bar window(s) above it.
// Lazily opened by EnsureMainWindowOpen() (output.cpp) -- not at glk_main() startup, but only once
// the game is loaded and its default colors (if any) are known, since glk_stylehint_set() only
// affects windows created *after* the call (see the Glk spec's "Suggesting the Appearance of
// Styles").
extern winid_t gMainWin;
extern strid_t gMainStream;
#ifdef SLGLK_STATUSBAR_JUSTIFIED_WINDOWS
// Three text buffer windows side by side above gMainWin: location (flexible width, left-flush),
// score (fixed width, centered -- reusing the style_BlockQuote/Centered hint already set up for
// the `<center>` tag), and the game's free-form user status text (fixed width, right-flush). See
// statusbar.cpp.
extern winid_t gStatusLocWin;
extern winid_t gStatusScoreWin;
extern winid_t gStatusUserWin;
#else
// A single text grid window above gMainWin, manually space-padded into columns by
// UpdateStatusBar() (statusbar.cpp), mirroring FrankenDrift's GlkRunner.
extern winid_t gStatusWin;
#endif
// Opens gMainWin/gMainStream/the status window(s) if they aren't already open. Called from
// glk_main() once the game's default colors are baked into style hints, and lazily by
// FatalError() so an error that occurs before then (an unreadable game file, or one CreateGame
// rejects outright) still has somewhere to print -- in that case with no game-specific style
// hints, since none are known.
void EnsureMainWindowOpen();

// Default foreground colors for ordinary output and for `<c>`-styled (player-command-like) text,
// applied by OutputStyled() whenever it isn't given an explicit color -- set from the game's own
// OutputColour/InputColour once loaded (see starlane-glk.cpp), and left at zcolor_Default (i.e.
// "let the terminal choose") for a game that doesn't specify either. This is a per-run fallback on
// top of (not a replacement for) the style_Normal/style_Input TextColor hints starlane-glk.cpp
// also sets: it's what colors text on Glk libraries that support the garglk zcolor extension but
// would otherwise print an explicit `<font color>` override in the wrong default color, and it's
// the *only* mechanism on libraries that don't implement stylehint_TextColor at all -- whereas the
// hints are what colors things on libraries that implement stylehint_TextColor but not zcolor.
extern uint32_t gDefaultOutputColor;
extern uint32_t gDefaultInputColor;

// strutils.cpp: UTF-8/UTF-32 conversion and case-folding helpers.
std::vector<uint32_t> Utf8ToUtf32(const std::string &s);
std::string Utf32ToUtf8(const uint32_t *buf, size_t count);
std::string Utf32ToUtf8(const std::vector<uint32_t> &buf);
// Removes the last complete UTF-8 codepoint from `s` (for the `<del>` tag), rather than just the
// last byte, so we never leave a dangling continuation byte behind.
void Utf8PopBack(std::string &s);

std::string StrToUpperCase(const std::string &str);
std::string StrToLowerCase(const std::string &str);
std::string StrToSentenceCase(const std::string &str);

// output.cpp: styled text output, ADRIFT's HTML-like markup, and line/char input.
void FatalError(const char *msg);
void OutputText(const char *msg);
// Parses a string that may contain ADRIFT's HTML-like output markup (`<b>`, `<i>`, `<c>`,
// `<center>`, `<font ...>`, `<br>`, `<cls>`, `<waitkey>`, `<del>`, `<img>`, `<audio ...>`, ...; see
// clsUserSession.vb's bHasOutput for the canonical tag list) and writes styled text (and any
// images/sounds) to the main window.
void AppendHtml(const std::string &html);
// Writes `text` to the main window using the closest available Glk style for `styleFlags`, and
// (via the garglk zcolor extension, where available) the given 24-bit RGB foreground color.
// 0xffffffff mirrors glkext.h's zcolor_Default.
void OutputStyled(const std::string &text, uint32_t styleFlags, uint32_t color = 0xffffffff);
// Prints `prompt` (styled as player input) and blocks until the player has entered a line of
// input on the main window, returning it as UTF-8. The Glk spec forbids printing to a window with
// line input pending, so if a real-time event fires while waiting and wants to print something,
// this briefly cancels the pending line request (glk_cancel_line_event() hands back whatever the
// player had typed so far, as if they'd pressed ENTER), tears down and reprints `prompt` around
// the event's text, and re-requests line input with that partial input intact so editing can
// resume where it left off.
std::string GetLineInput(const std::string &prompt);
// Blocks until the player has pressed a key on the main window (for the `<waitkey>` tag). Text a
// real-time event wants to print while waiting is buffered rather than written to the window
// (same rationale as GetLineInput()'s cancel-and-restore dance, but with no partial input to
// preserve there's nothing to do but hold onto it) and flushed once the keypress arrives.
void WaitForKeypress();
// Attempts to erase the last character of the most recently *flushed* output (for `<del>`, once
// there's nothing left in the current run to remove without having committed it to the window).
void UnputLastChar();
bool AskYesNoQuestion(const char *question);

// statusbar.cpp: keeps the status window(s) in sync with starlane-core's GetStatusBar(). Call
// after BeginGame(), after each ProcessInput(), and after each TimeTick() -- same as
// Starlane::GetStatusBar() itself asks for. A no-op if the status window(s) aren't open yet, or
// if the game hasn't begun.
void UpdateStatusBar();

// multimedia.cpp: <img>/<audio> tag handling, backed by starlane-core's Blorb resource mapping.
// Checks gestalts and sets up sound channels; must be called once during startup, after the main
// window has been opened and before any <img>/<audio> tag can be processed.
void InitMultimedia();
// Draws the image at `path` (an author-side file path, resolved through starlane-core's blorb
// file mapping) scaled to fill 100% of either the window's width or height, whichever is the
// smaller scale factor -- i.e. as large as possible without spilling outside either dimension.
void DrawImageFitted(const std::string &path);
// `channel` is assumed already validated to be within [1, 8] -- AppendHtml, the only caller, is
// the boundary where that untrusted tag content gets checked.
void PlaySound(const std::string &path, int channel, bool loop);
void PauseSound(int channel);
void StopSound(int channel);

// savefile.cpp: save/restore file callbacks for starlane-core.
void *CreateSaveFile();
void *OpenSaveFile();
size_t ReadFile(void *ptr, uint8_t *buffer, size_t bufsize);
void WriteFile(void *ptr, const uint8_t *buffer, size_t count);
void CloseFile(void *ptr);

#endif //STARLANE_GLK_INTERNAL_H
