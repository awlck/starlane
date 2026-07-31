//
// Created by Adrian Welcker on 17.07.26.
//

#include "starlane-glk-internal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

void EnsureMainWindowOpen() {
	if (gMainWin) return;
	gMainWin = glk_window_open(nullptr, 0, 0, wintype_TextBuffer, 0);
	if (!gMainWin) glk_exit();
	gMainStream = glk_window_get_stream(gMainWin);

	// A style hint only affects windows opened *after* the call -- gMainWin, just above, already
	// exists by this point, so setting ReverseColor here gives the status window(s) below a reverse
	// video look without touching the main window's own use of the same style number(s).
#ifdef SLGLK_STATUSBAR_JUSTIFIED_WINDOWS
	// Each status column uses a distinct style (see statusbar.cpp/starlane-glk.cpp), all TextBuffer
	// windows here, so each needs its own ReverseColor hint.
	glk_stylehint_set(wintype_TextBuffer, style_Normal, stylehint_ReverseColor, 1);
	glk_stylehint_set(wintype_TextBuffer, style_BlockQuote, stylehint_ReverseColor, 1);
	glk_stylehint_set(wintype_TextBuffer, style_User1, stylehint_ReverseColor, 1);

	// Carve the status area into three columns, from the right inward: the score and user-status
	// columns get a fixed character width (as measured in their own font, per the Glk spec), and
	// whatever's left of the row -- the flexible part -- ends up as the location column.
	constexpr glui32 kScoreColumnWidth = 14;   // fits "Score: -999999" (an implausibly large score)
	constexpr glui32 kUserColumnWidth = 24;
	gStatusLocWin = glk_window_open(gMainWin, winmethod_Above | winmethod_Fixed, 1, wintype_TextBuffer, 0);
	if (gStatusLocWin) {
		gStatusUserWin = glk_window_open(gStatusLocWin, winmethod_Right | winmethod_Fixed, kUserColumnWidth, wintype_TextBuffer, 0);
		gStatusScoreWin = glk_window_open(gStatusLocWin, winmethod_Right | winmethod_Fixed, kScoreColumnWidth, wintype_TextBuffer, 0);
	}
#else
	// UpdateStatusBar() (statusbar.cpp) never calls glk_set_style_stream() on gStatusWin's stream,
	// so it stays at style_Normal (a newly opened window's default style) the whole time.
	glk_stylehint_set(wintype_TextGrid, style_Normal, stylehint_ReverseColor, 1);
	gStatusWin = glk_window_open(gMainWin, winmethod_Above | winmethod_Fixed, 1, wintype_TextGrid, 0);
#endif
}

void FatalError(const char *msg) {
	// Reachable before glk_main() has opened a window at all -- e.g. the game file couldn't be
	// read, or CreateGame() rejected its contents outright -- in which case there is nowhere yet
	// for this message to go.
	EnsureMainWindowOpen();
	OutputStyled("\n", kStyleNormal);
	OutputStyled(msg, kStyleBold);
	OutputStyled("\n", kStyleNormal);
}

namespace {
// Set by WaitForKeypress()/GetLineInput() while a Glk char- or line-input request is pending on
// gMainWin, so OutputText() knows to hold onto text a real-time event wants to print instead of
// handing it to AppendHtml() -- the Glk spec says printing to a window with line input pending is
// illegal, and we apply the same rule to char input for consistency. Drained by whichever of the
// two functions set the flag, once their glk_select() loop notices a timer tick actually produced
// something; see each function for what it does with the captured text from there.
bool gCapturingOutput = false;
std::string gCapturedHtml;
}  // namespace

void OutputText(const char *msg) {
	if (gCapturingOutput) {
		gCapturedHtml += msg;
		return;
	}
	AppendHtml(msg);
}

uint32_t gDefaultOutputColor = zcolor_Default;
uint32_t gDefaultInputColor = zcolor_Default;

namespace {

// The Unicode text of the most recently *flushed* OutputStyled() call, tracked so that `<del>`
// has something to try to unput once the current (not-yet-flushed) run is already empty.
std::vector<uint32_t> gMostRecentOutput;

// Attempts to erase `text` from the end of the most recently flushed output -- used by
// GetLineInput() to tear down the prompt it printed before reprinting it around timer-driven
// output. A silent no-op if the Glk library doesn't support the garglk extension, or if `text` no
// longer matches the window's actual tail (nothing else should have been printed in between, but
// this keeps a mismatch harmless instead of corrupting the display further).
void UnputText(const std::string &text) {
	if (text.empty()) return;
	auto codepoints = Utf8ToUtf32(text);
	codepoints.push_back(0);
	glk_set_window(gMainWin);
	if (garglk_unput_string_count_uni(codepoints.data()) > 0)
		gMostRecentOutput.clear();
}

// Locates the [start, end) byte range of `attr`'s value within `tagLower` (a lowercased tag
// body), handling both `attr="quoted value"` and `attr=unquoted` forms. Returns false if the
// attribute isn't present. ASCII case-folding never changes a string's length, so the range
// found here can be sliced out of either the lowercased tag body or the original one.
bool FindAttributeRange(const std::string &tagLower, const std::string &attr, size_t &start, size_t &end) {
	size_t pos = tagLower.find(attr);
	if (pos == std::string::npos) return false;
	pos += attr.size();
	while (pos < tagLower.size() && std::isspace((unsigned char) tagLower[pos])) pos++;
	if (pos >= tagLower.size() || tagLower[pos] != '=') return false;
	pos++;
	while (pos < tagLower.size() && std::isspace((unsigned char) tagLower[pos])) pos++;
	if (pos < tagLower.size() && (tagLower[pos] == '"' || tagLower[pos] == '\'')) {
		char quote = tagLower[pos++];
		size_t e = tagLower.find(quote, pos);
		if (e == std::string::npos) e = tagLower.size();
		start = pos;
		end = e;
	} else {
		size_t e = pos;
		while (e < tagLower.size() && !std::isspace((unsigned char) tagLower[e]) && tagLower[e] != '>') e++;
		start = pos;
		end = e;
	}
	return true;
}

// Extracts the value of `attr="..."` or `attr=...` from `tagLower`, a lowercased tag body.
// Returns false if the attribute isn't present.
bool ExtractAttribute(const std::string &tagLower, const std::string &attr, std::string &valueOut) {
	size_t start, end;
	if (!FindAttributeRange(tagLower, attr, start, end)) return false;
	valueOut = tagLower.substr(start, end - start);
	return true;
}

// Same as ExtractAttribute(), but returns the value with its original casing preserved (from
// `tagOriginal`, the same tag body before lowercasing) -- needed for `src` paths, which have to
// match ADRIFT's file-mapping keys exactly as the author wrote them.
bool ExtractAttributeOriginalCase(const std::string &tagLower, const std::string &tagOriginal,
                                   const std::string &attr, std::string &valueOut) {
	size_t start, end;
	if (!FindAttributeRange(tagLower, attr, start, end)) return false;
	valueOut = tagOriginal.substr(start, end - start);
	return true;
}

// Parses a `<font color="...">` attribute, either a `#`-optional hex triplet (e.g. the game data
// seen in the wild uses both `color = red` and `color = FDD017`) or one of the named colors
// ADRIFT's own runner recognizes (per FrankenDrift's GlkHtmlWin.cs, cross-referenced against the
// original VB source).
bool ParseFontColor(const std::string &tagLower, uint32_t &colorOut) {
	std::string value;
	if (!ExtractAttribute(tagLower, "color", value)) return false;
	if (!value.empty() && value[0] == '#') value = value.substr(1);
	bool isHex = value.size() == 6 && std::all_of(value.begin(), value.end(), [](char c) {
		return std::isxdigit((unsigned char) c) != 0;
	});
	if (isHex) {
		colorOut = (uint32_t) std::strtoul(value.c_str(), nullptr, 16);
		return true;
	}
	static const std::unordered_map<std::string, uint32_t> kNamedColors = {
		{ "black", 0x000000 }, { "blue", 0x0000ff }, { "gray", 0x808080 }, { "grey", 0x808080 },
		{ "darkgreen", 0x006400 }, { "green", 0x008000 }, { "lime", 0x00ff00 }, { "magenta", 0xff00ff },
		{ "maroon", 0x800000 }, { "navy", 0x000080 }, { "olive", 0x808000 }, { "orange", 0xffa500 },
		{ "pink", 0xffc0cb }, { "purple", 0x800080 }, { "red", 0xff0000 }, { "silver", 0xc0c0c0 },
		{ "teal", 0x008080 }, { "white", 0xffffff }, { "yellow", 0xffff00 }, { "cyan", 0x00ffff },
		{ "darkolive", 0x556b2f }, { "tan", 0xd2b48c },
	};
	auto it = kNamedColors.find(value);
	if (it == kNamedColors.end()) return false;
	colorOut = it->second;
	return true;
}

// Whether `face` (already lowercased) names one of the fonts commonly used to ask for a
// fixed-width look, so a `<font face="...">` without an explicit `<tt>`-like tag still gets
// mapped onto style_Preformatted. List cross-referenced from FrankenDrift's GlkHtmlWin.cs.
bool IsMonospaceFace(const std::string &face) {
	static const std::unordered_set<std::string> kMonospaceFaces = {
		"andale mono", "cascadia code", "century schoolbook monospace", "consolas", "courier",
		"courier new", "liberation mono", "ubuntu mono", "dejavu sans mono", "droid sans mono",
		"lucida console", "menlo", "ocr-a", "ocr-a extended", "overpass mono", "oxygen mono",
		"roboto mono", "source code pro", "everson mono", "fira mono", "fixed", "fixedsys",
		"freemono", "go mono", "hyperfont", "ibm mda", "ibm plex mono", "inconsolata", "iosevka",
		"letter gothic", "monaco", "monofur", "monospace", "monospace (unicode)", "nimbus mono l",
		"noto mono", "nk57 monospace", "ocr-b", "pragmatapro", "prestige elite", "profont",
		"pt mono", "spleen", "terminus", "tex gyre cursor", "american typewriter", "tads-monospace",
	};
	return kMonospaceFaces.count(face) > 0;
}

}  // namespace

bool AskYesNoQuestion(const char *question) {
	OutputStyled("\n", kStyleNormal);
	OutputStyled(question, kStyleNormal);
	for (;;) {
		std::string answer = StrToLowerCase(GetLineInput("\n[yes/no] > "));
		if (answer == "y" || answer == "yes") return true;
		if (answer == "n" || answer == "no") return false;
	}
}

void OutputStyled(const std::string &text, uint32_t styleFlags, uint32_t color) {
	if (text.empty()) return;
	glui32 style;
	if (styleFlags & kStyleMonospace) style = style_Preformatted;
	else if (styleFlags & kStyleCentered) style = style_BlockQuote;
	else if ((styleFlags & kStyleBold) && (styleFlags & kStyleItalic)) style = style_Alert;
	else if (styleFlags & kStyleItalic) style = style_Emphasized;
	else if (styleFlags & kStyleBold) style = style_Subheader;
	else if (styleFlags & kStyleInput) style = style_Input;
	else style = style_Normal;

	// A caller with no opinion on color (the common case) gets the game's own default for this
	// style, if it specified one -- otherwise zcolor_Default, i.e. "let the terminal choose".
	uint32_t effectiveColor = color;
	if (effectiveColor == zcolor_Default)
		effectiveColor = (styleFlags & kStyleInput) ? gDefaultInputColor : gDefaultOutputColor;

	// Harmless no-op if the underlying Glk library doesn't support the garglk color extension.
	garglk_set_zcolors_stream(gMainStream, effectiveColor, zcolor_Default);
	glk_set_style_stream(gMainStream, style);
	auto codepoints = Utf8ToUtf32(text);
	glk_put_buffer_stream_uni(gMainStream, (glui32 *) codepoints.data(), (glui32) codepoints.size());
	gMostRecentOutput = std::move(codepoints);
}

void UnputLastChar() {
	if (gMostRecentOutput.empty()) return;
	glui32 buf[2] = { gMostRecentOutput.back(), 0 };
	glk_set_window(gMainWin);
	if (garglk_unput_string_count_uni(buf) > 0)
		gMostRecentOutput.pop_back();
}

void WaitForKeypress() {
	glk_request_char_event(gMainWin);
	event_t ev;
	for (;;) {
		glk_select(&ev);
		if (ev.type == evtype_CharInput && ev.win == gMainWin) return;
		if (ev.type == evtype_Timer) {
			gCapturingOutput = true;
			Starlane::TimeTick();
			gCapturingOutput = false;
			UpdateStatusBar();
			// Nothing was typed here to preserve (unlike GetLineInput()'s cancel/restore dance),
			// so a keystroke is still pending and all there is to do is show what was captured.
			if (!gCapturedHtml.empty()) {
				std::string html = std::move(gCapturedHtml);
				gCapturedHtml.clear();
				AppendHtml(html);
			}
		}
	}
}

std::string GetLineInput(const std::string &prompt) {
	constexpr glui32 kCapacity = 256;
	std::vector<glui32> buf(kCapacity);
	glui32 initlen = 0;
	// The library's own after-the-fact echo of the completed/cancelled line (see the Glk spec on
	// glk_set_echo_line_event()) would print in whatever style was active during composition,
	// which we don't want to rely on -- we echo submitted commands ourselves, below, in the Input
	// style. Turning it off is also what makes the cancel-and-restore dance below leave no trace
	// of the player's not-yet-submitted partial input on screen, so all that's left to tear down
	// around a timer tick's output is the prompt itself.
	glk_set_echo_line_event(gMainWin, 0);
	OutputStyled(prompt, kStyleInput);
	glk_request_line_event_uni(gMainWin, buf.data(), kCapacity, initlen);
	event_t ev;
	for (;;) {
		glk_select(&ev);
		if (ev.type == evtype_LineInput && ev.win == gMainWin) {
			std::string result = Utf32ToUtf8(buf.data(), ev.val1);
			OutputStyled(result, kStyleInput);
			OutputStyled("\n", kStyleNormal);
			return result;
		}
		if (ev.type == evtype_Timer) {
			gCapturingOutput = true;
			Starlane::TimeTick();
			gCapturingOutput = false;
			// Safe to update even here: the status window is never the one with a pending input
			// request, so this doesn't run into the output-during-input restriction below.
			UpdateStatusBar();
			if (gCapturedHtml.empty()) continue;
			std::string html = std::move(gCapturedHtml);
			gCapturedHtml.clear();

			// glk_cancel_line_event() fills buf/ev.val1 with whatever the player had typed so
			// far, exactly as if they'd pressed ENTER -- we carry that length into the
			// re-request below as `initlen` so editing resumes where it left off.
			event_t cancelEv;
			glk_cancel_line_event(gMainWin, &cancelEv);
			initlen = cancelEv.val1;
			UnputText(prompt);
			AppendHtml(html);
			OutputStyled(prompt, kStyleInput);
			glk_request_line_event_uni(gMainWin, buf.data(), kCapacity, initlen);
		}
	}
}

void AppendHtml(const std::string &html) {
	if (html.empty()) return;

	struct StyleFrame {
		uint32_t flags;
		uint32_t color;
		std::string tag;
	};
	std::vector<StyleFrame> styleStack;
	styleStack.push_back({ kStyleNormal, zcolor_Default, "<base>" });

	std::string current;
	bool inTag = false;
	std::string tagBuf;

	auto flush = [&]() {
		if (!current.empty()) {
			OutputStyled(current, styleStack.back().flags, styleStack.back().color);
			current.clear();
		}
	};

	for (char c : html) {
		if (c == '<' && !inTag) {
			inTag = true;
			tagBuf.clear();
		} else if (inTag && c != '>') {
			tagBuf += c;
		} else if (inTag && c == '>') {
			inTag = false;
			std::string tagLower = tagBuf;
			for (char &tc : tagLower) tc = (char) std::tolower((unsigned char) tc);
			std::string tagWord = tagLower.substr(0, tagLower.find_first_of(" \t"));

			if (tagWord == "del") {
				// Prefer removing from the not-yet-flushed run; only fall back to trying to
				// unput already-committed output (via the garglk extension) once that's empty.
				if (!current.empty()) Utf8PopBack(current);
				else UnputLastChar();
				continue;
			}

			flush();
			uint32_t curFlags = styleStack.back().flags;
			uint32_t curColor = styleStack.back().color;
			if (tagWord == "br") {
				OutputStyled("\n", curFlags, curColor);
			} else if (tagWord == "cls") {
				styleStack.assign(1, StyleFrame{ kStyleNormal, zcolor_Default, "<base>" });
				glk_window_clear(gMainWin);
			} else if (tagWord == "waitkey") {
				WaitForKeypress();
			} else if (tagWord == "b") {
				styleStack.push_back({ curFlags | kStyleBold, curColor, "b" });
			} else if (tagWord == "i") {
				styleStack.push_back({ curFlags | kStyleItalic, curColor, "i" });
			} else if (tagWord == "u") {
				// Glk has no underline style; tracked only so `</u>` balances the stack.
				styleStack.push_back({ curFlags, curColor, "u" });
			} else if (tagWord == "c") {
				styleStack.push_back({ curFlags | kStyleInput, curColor, "c" });
			} else if (tagWord == "center" || tagWord == "centre") {
				styleStack.push_back({ curFlags | kStyleCentered, curColor, "center" });
			} else if (tagWord == "left" || tagWord == "right") {
				// No distinct Glk alignment for these; tracked only so the closing tag balances.
				styleStack.push_back({ curFlags, curColor, tagWord });
			} else if (tagWord == "font") {
				// A `<font>` tag without a `color` attribute (e.g. one that only sets `size`)
				// keeps whatever color is already in effect, rather than resetting to default.
				uint32_t newFlags = curFlags;
				uint32_t newColor = curColor;
				uint32_t parsedColor;
				if (ParseFontColor(tagLower, parsedColor)) newColor = parsedColor;
				std::string face;
				if (ExtractAttribute(tagLower, "face", face) && IsMonospaceFace(face))
					newFlags |= kStyleMonospace;
				styleStack.push_back({ newFlags, newColor, "font" });
			} else if (!tagWord.empty() && tagWord[0] == '/') {
				std::string closeName = tagWord.substr(1);
				if (closeName == "centre") closeName = "center";
				if (styleStack.size() > 1 && styleStack.back().tag == closeName)
					styleStack.pop_back();
			} else if (tagWord == "img") {
				std::string src;
				if (ExtractAttributeOriginalCase(tagLower, tagBuf, "src", src) && !src.empty())
					DrawImageFitted(src);
			} else if (tagLower.rfind("audio play", 0) == 0) {
				std::string src;
				if (ExtractAttributeOriginalCase(tagLower, tagBuf, "src", src) && !src.empty()) {
					int channel = 1;
					std::string channelStr;
					if (ExtractAttribute(tagLower, "channel", channelStr))
						channel = std::atoi(channelStr.c_str());
					if (channel >= 1 && channel <= 8)
						PlaySound(src, channel, tagLower.find("loop=y") != std::string::npos);
				}
			} else if (tagLower.rfind("audio pause", 0) == 0) {
				int channel = 1;
				std::string channelStr;
				if (ExtractAttribute(tagLower, "channel", channelStr))
					channel = std::atoi(channelStr.c_str());
				if (channel >= 1 && channel <= 8) PauseSound(channel);
			} else if (tagLower.rfind("audio stop", 0) == 0) {
				int channel = 1;
				std::string channelStr;
				if (ExtractAttribute(tagLower, "channel", channelStr))
					channel = std::atoi(channelStr.c_str());
				if (channel >= 1 && channel <= 8) StopSound(channel);
			}
			// Anything else is not (yet) recognized and is silently dropped.
		} else {
			current += c;
		}
	}
	flush();
}

void TranscriptOn() {
	if (!gMainWin) return;
	if (glk_window_get_echo_stream(gMainWin)) {
		AppendHtml("<i>Transcript is already on; use <font face=\"Courier\">!scriptoff</font> to disable it.</i>\n");
		return;
	}
	auto fileref = glk_fileref_create_by_prompt(fileusage_Transcript | fileusage_TextMode, filemode_Write, 0);
	if (!fileref) {
		AppendHtml("<i>Transcript activation canceled.</i>\n");
		return;
	}
	auto stream = glk_stream_open_file(fileref, filemode_Write, 0);
	if (stream) {
		glk_window_set_echo_stream(gMainWin, stream);
		AppendHtml("<i>Transcript started.</i>\n");
	} else {
		AppendHtml("<i>Transcript activation failed, sorry.</i>\n");
	}
	glk_fileref_destroy(fileref);
}

void TranscriptOff() {
	if (!gMainWin) return;
	strid_t echostream;
	if (!(echostream = glk_window_get_echo_stream(gMainWin))) {
		AppendHtml("<i>Transcript is not running; use <font face=\"Courier\">!scripton</font> to start it.</i>\n");
		return;
	}
	glk_window_set_echo_stream(gMainWin, nullptr);
	glk_stream_close(echostream, nullptr);
	AppendHtml("<i>Transcript stopped.</i>\n");
}