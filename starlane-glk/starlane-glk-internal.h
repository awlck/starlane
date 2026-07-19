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

// The main (text buffer) output window and its stream, and the status bar above it. Created in
// starlane-glk.cpp's glk_main(); starlane-core does not yet expose a callback for status bar
// content (location/score), so for now the status window is created but left blank -- TODO once
// such a callback exists.
extern winid_t gMainWin;
extern winid_t gStatusWin;
extern strid_t gMainStream;

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
// Blocks until the player has entered a line of input on the main window, returning it as UTF-8.
std::string GetLineInput();
// Blocks until the player has pressed a key on the main window (for the `<waitkey>` tag).
void WaitForKeypress();
// Attempts to erase the last character of the most recently *flushed* output (for `<del>`, once
// there's nothing left in the current run to remove without having committed it to the window).
void UnputLastChar();
bool AskYesNoQuestion(const char *question);

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
