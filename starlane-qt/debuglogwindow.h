//
// A dockable panel showing starlane-core's debug/trace events (see
// Starlane::SetDebugEventCallback in starlane-core.h) -- matching/selecting tasks, restriction
// checks, event/walk scheduling, variable writes, and so on -- with a per-category filter.
//
// Laziness lives at two levels, matching starlane-core's own: the engine already skips building a
// message at all for a category nobody has enabled (see debuglog.h's SL_DEBUG macro), and this
// window only enables a category with starlane-core in the first place while it is both visible
// and checked in the category filter -- so a closed (or category-filtered) panel costs nothing
// beyond the one function-pointer callback registration.

#ifndef STARLANE_DEBUGLOGWINDOW_H
#define STARLANE_DEBUGLOGWINDOW_H

#include <array>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QPlainTextEdit>

#include <starlane-core.h>

class DebugLogWindow : public QDockWidget {
	Q_OBJECT
public:
	explicit DebugLogWindow(QWidget *parent = nullptr);

	// Appends one already-formatted event line. `message` is a plain (non-HTML) string; the caller
	// (MainWindow's debug-event callback) owns converting the core's `const char *` to a QString
	// before starlane-core's own buffer behind it goes away.
	void AppendEvent(Starlane::DebugCategory category, const QString &message);

private:
	QPlainTextEdit *log;
	// One checkbox per DebugCategory, indexed by (uint32_t) category -- checked means "show this
	// category while the panel is visible". All start checked, so opening the panel for the first
	// time shows everything, same as starlane-console does unconditionally.
	std::array<QCheckBox *, Starlane::kDebugCategoryCount> categoryChecks;

	// Tells starlane-core which categories to actually produce events for right now: every category
	// whose action is checked, if (and only if) this panel is currently visible -- otherwise none.
	// Called whenever visibility changes or a category checkbox is toggled.
	void ApplyEnabledCategories();

	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
};

#endif  // !STARLANE_DEBUGLOGWINDOW_H
