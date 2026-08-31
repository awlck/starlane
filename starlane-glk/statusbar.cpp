//
// Created by Adrian Welcker on 30.07.26.
//

#include "starlane-glk-internal.h"

#include <regex>

namespace {

std::string FormatScore(const Starlane::StatusBar &statusBar) {
	return statusBar.scoringUsed ? ("Score: " + std::to_string(statusBar.score)) : "";
}

std::string StripTags(const std::string &text) {
	static std::regex tagStripRegex("<.+?>", std::regex::ECMAScript | std::regex::optimize | std::regex::multiline);
	return std::regex_replace(text, tagStripRegex, "");
}

}  // namespace

#ifdef SLGLK_STATUSBAR_JUSTIFIED_WINDOWS

namespace {

// Rewrites `win`'s (single-line) contents to `text`, styled with `style` -- whose justification
// hint (set once in starlane-glk.cpp, before any window existed) determines where within the
// column `text` ends up lining up.
void SetColumnText(winid_t win, glui32 style, const std::string &text) {
	if (!win) return;
	glk_window_clear(win);
	if (text.empty()) return;
	strid_t str = glk_window_get_stream(win);
	glk_set_style_stream(str, style);
	auto codepoints = Utf8ToUtf32(text);
	glk_put_buffer_stream_uni(str, (glui32 *) codepoints.data(), (glui32) codepoints.size());
}

}  // namespace

void UpdateStatusBar() {
	if (!gStatusLocWin) return;
	Starlane::StatusBar sb;
	if (!Starlane::GetStatusBar(sb)) return;

	SetColumnText(gStatusLocWin, style_Normal, StripTags(sb.location));
	// style_BlockQuote's Justification hint is Centered -- see the comment in starlane-glk.cpp.
	SetColumnText(gStatusScoreWin, style_BlockQuote, FormatScore(sb));
	// style_User1's Justification hint is RightFlush -- likewise.
	SetColumnText(gStatusUserWin, style_User1, StripTags(sb.userStatus));
}

#else  // !SLGLK_STATUSBAR_JUSTIFIED_WINDOWS

// Manually space-pads location/score/user-status into columns of a single text grid window,
// mirroring FrankenDrift's GlkRunner (see clsUserSession.vb's UpdateStatusBar()).
void UpdateStatusBar() {
	if (!gStatusWin) return;
	Starlane::StatusBar sb;
	if (!Starlane::GetStatusBar(sb)) return;
	std::string score = FormatScore(sb);
	std::string userStatus = StripTags(sb.userStatus);
	std::string location = StripTags(sb.location);

	glui32 width = 0, height = 0;
	glk_window_get_size(gStatusWin, &width, &height);
	glk_window_clear(gStatusWin);
	if (width == 0) return;

	std::string line;
	if (userStatus.empty()) {
		int spaces = (int) width - (int) location.size() - (int) score.size();
		if (spaces < 2) spaces = 2;
		line = location + std::string(spaces, ' ') + score;
	} else {
		int spaces = ((int) width - (int) location.size() - (int) score.size() - (int) userStatus.size()) / 2;
		if (spaces < 2) spaces = 2;
		line = location + std::string(spaces, ' ') + score + std::string(spaces, ' ') + userStatus;
	}

	auto codepoints = Utf8ToUtf32(line);
	if (codepoints.size() > width) codepoints.resize(width);
	glk_window_move_cursor(gStatusWin, 0, 0);
	glk_put_buffer_stream_uni(glk_window_get_stream(gStatusWin), (glui32 *) codepoints.data(), (glui32) codepoints.size());
}

#endif
