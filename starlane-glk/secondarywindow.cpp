//
// Created for <window NAME> support in the Glk frontend.
//

#include "starlane-glk-internal.h"

#include <unordered_map>

namespace {

std::unordered_map<std::string, SecondaryWindow> gSecondaryWindows;
// The most recently created secondary window, so the next one splits IT rather than the main
// window again -- see GetOrCreateSecondaryWindow()'s own doc comment for why.
winid_t gLastSecondaryWin = nullptr;

}  // namespace

SecondaryWindow *GetOrCreateSecondaryWindow(const std::string &name) {
	auto it = gSecondaryWindows.find(name);
	if (it != gSecondaryWindows.end()) return &it->second;

	// Unlike the Qt frontend's player-movable/floatable QDockWidgets, Glk gives games (and us) no
	// say in where a window ends up beyond a fixed, one-time tree of splits -- there's no way to
	// undock, rearrange, or resize windows relative to each other after the fact, only the whole
	// application window as a unit. ADRIFT itself assumes the opposite (author-placed, freely
	// movable windows), so there's no layout information to work from; we just pick something
	// reasonable instead: the first secondary window takes the right third of the main window, and
	// every one after that splits the most recently created secondary window in half, cascading
	// down that same column rather than piling up next to the main window or against each other.
	winid_t win;
	if (!gLastSecondaryWin)
		win = glk_window_open(gMainWin, winmethod_Right | winmethod_Proportional, 33, wintype_TextBuffer, 0);
	else
		win = glk_window_open(gLastSecondaryWin, winmethod_Below | winmethod_Proportional, 50, wintype_TextBuffer, 0);
	if (!win) return nullptr;  // the library couldn't create it (e.g. no more room to split into)

	strid_t stream = glk_window_get_stream(win);
	auto result = gSecondaryWindows.emplace(name, SecondaryWindow{win, stream, {}});
	gLastSecondaryWin = win;
	return &result.first->second;
}
