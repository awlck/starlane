#include <QtCore/QElapsedTimer>
#include <QtCore/QProcess>
#include <QtCore/QString>
#include <QtWidgets/QApplication>

#include <starlane-core.h>
#include <cctype>
#include <clocale>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtGui/QFileOpenEvent>
#include <QtGui/QPalette>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyleFactory>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#endif

#include "mainwindow.h"

MainWindow *theWin = nullptr;

namespace {

// Catches the macOS "open with"/double-click delivery mechanism (QFileOpenEvent), which arrives
// as a system event rather than a command-line argument the way it does on Windows and Linux. It
// can in principle arrive before MainWindow exists yet, so a path received that early is stashed
// and handed to main() to deliver once the window is up.
class StarlaneApplication : public QApplication {
public:
	using QApplication::QApplication;

	QString TakePendingOpenPath() {
		QString path = pendingOpenPath;
		pendingOpenPath.clear();
		return path;
	}

protected:
	bool event(QEvent *event) override {
		if (event->type() == QEvent::FileOpen) {
			const auto *openEvent = static_cast<QFileOpenEvent *>(event);
			const QString path = openEvent->file();
			if (!theWin) {
				pendingOpenPath = path;
			} else if (Starlane::GameIsOngoing()) {
				// macOS reuses this already-running process for "open with"/double-click rather
				// than launching a fresh one -- but Game::Get() (game.h) is a single global
				// instance, so we can't just load a second game alongside the one already
				// running here. Rather than steal focus and make the player choose between their
				// current game and the one they just opened, hand this file to a brand new
				// instance of ourselves instead, exactly as if it had been launched directly.
				// (This is the same as Option-double-clicking, or `open -n`: macOS is fine with
				// several processes sharing one bundle identifier, since the "reuse the running
				// instance" behavior above is just Launch Services' default routing for an
				// open-document request, not a hard constraint on the bundle -- going around it
				// like this doesn't confuse Launch Services or the Dock.)
#ifndef __EMSCRIPTEN__
				QProcess::startDetached(QCoreApplication::applicationFilePath(), {path});
#endif
			} else {
				// No game ongoing (either none loaded yet, or the last one ended and was fully
				// quit): safe to just load it here. The window may be minimized or behind others
				// though, so surface it explicitly -- otherwise the newly loaded game could end
				// up showing behind other windows, unseen.
				theWin->show();
				theWin->raise();
				theWin->activateWindow();
				theWin->LoadGameFile(path);
			}
			return true;
		}
		return QApplication::event(event);
	}

private:
	QString pendingOpenPath;
};

}  // namespace

namespace SlQt {

void FatalError(const char *msg) {
	theWin->OutputText("<font color=\"red\"><b>Fatal error</b>:</font> ");
	theWin->OutputText(msg);
}

void OutputText(const char *msg) {
	theWin->OutputText(msg);
}

void DebugEvent(Starlane::DebugCategory category, const char *msg) {
	theWin->OnDebugEvent(category, msg);
}

std::string StrToLowerCase(const std::string &s) {
	QString lower = QString::fromUtf8(s.c_str(), s.length()).toLower();
	return lower.toUtf8().toStdString();
}

std::string StrToUpperCase(const std::string &s) {
	return QString::fromUtf8(s.c_str(), s.length()).toUpper().toUtf8().toStdString();
}

std::string StrToSentenceCase(const std::string &s) {
	QList<uint> codepoints = QString::fromUtf8(s.c_str(), s.length()).toLower().toUcs4();
	for (qsizetype i = 0; i < codepoints.size(); i++) {
		char32_t cp = codepoints[i];
		if (cp < 128 && std::isspace((int) cp)) continue;
		// A single-character buffer's upper-case is used in place of proper title-casing here, to
		// match the Glk frontend's StrToSentenceCase (see starlane-glk/strutils.cpp).
		QList<uint> upper = QString::fromUcs4(&cp, 1).toUpper().toUcs4();
		codepoints.remove(i, 1);
		for (qsizetype k = upper.size() - 1; k >= 0; k--)
			codepoints.insert(i, upper[k]);
		break;
	}
	QList<char32_t> codepoints32(codepoints.begin(), codepoints.end());
	return QString::fromUcs4(codepoints32.data(), codepoints32.size()).toUtf8().toStdString();
}

#ifdef __EMSCRIPTEN__
// AskYesNo() below needs a real synchronous bool back -- its only caller, Game::
// AttemptMatchEndOfGameCommand()'s QUIT handling (parser.cpp), decides right there whether to
// proceed based on the return value, the same way file access needs actual bytes back rather
// than a promise of some arriving later. QMessageBox::question()'s QDialog::exec() can't do that
// on WebAssembly's real browser main thread, but window.confirm() -- an old, native browser API,
// not a Qt one -- genuinely can: unlike anything Qt itself offers there, it's actually,
// synchronously blocking, even on the main thread, so it works here with no restructuring at all.
// MainWindow::LoadGameData() (mainwindow.cpp) reuses this same helper for its own "discard the
// current game?" confirmation, for the same reason -- it needs the answer before deciding whether
// to proceed too.
EM_JS(int, WasmConfirm, (const char *question), {
	return confirm(UTF8ToString(question)) ? 1 : 0;
});
#endif

bool AskYesNo(const char *question) {
#ifdef __EMSCRIPTEN__
	return WasmConfirm(question) != 0;
#else
	return QMessageBox::question(theWin, "Starlane", QString::fromUtf8(question),
	                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
#endif
}

void QuitGame() {
#ifdef __EMSCRIPTEN__
	// QApplication::quit() -- appropriate on every other platform, where this is a whole desktop
	// app's own window to close -- goes through Qt's synchronous window-closing machinery
	// (QGuiApplicationPrivate::quit() -> ... -> QWindowSystemInterface::
	// handleApplicationTermination<SynchronousDelivery>()), which needs Asyncify on WebAssembly's
	// real browser main thread and aborts outright without it (confirmed empirically: answering
	// "yes" to the QUIT confirmation crashes the tab). There's also no real equivalent action to
	// take here in the first place -- this is a browser tab, not a process, and nothing closes it
	// for the player short of them doing that themselves. Game::AttemptMatchEndOfGameCommand()
	// (parser.cpp), this function's only caller, has already cleared gameHasBegun/sessionActive
	// before calling it, so the engine's own state is already consistent; MainWindow::
	// SubmitCommand()'s post-command UpdateActionState()/UpdateStatusBar() calls already reflect
	// that the session has ended (Save/Restore/Transcript/Replay disabled) the same way they do
	// for any other way a game can end.
	OutputText("\n\n<i>The game has ended.</i>\n");
#else
	QApplication::quit();
#endif
}

void PumpEvents() {
	// The engine calls this liberally -- e.g. once per turn skipped by a "skip N turns" action --
	// on the assumption that actually pumping is cheap or self-limiting (see Frontend::PumpEvents's
	// doc comment). It isn't: Gargoyle's own Qt-based Glk library found that calling
	// QApplication::processEvents() unconditionally at that frequency (there, once per VM opcode)
	// noticeably slowed down exactly the busy stretches this exists to keep responsive during. So,
	// like Gargoyle, only actually pump once some minimum interval has passed.
	static QElapsedTimer sinceLastPump;
	constexpr qint64 kMinIntervalMs = 15;
	if (sinceLastPump.isValid() && sinceLastPump.elapsed() < kMinIntervalMs) return;
	sinceLastPump.start();

	// Excludes user input: this can be called from deep inside Starlane::ProcessInput() (a "skip N
	// turns" action mid-turn), and MainWindow::InputReturnPressed() has no guard against being
	// invoked again while an outer ProcessInput() call is still on the stack -- delivering a
	// pending keypress here could reenter the engine mid-turn and corrupt its bookkeeping.
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

#ifdef __EMSCRIPTEN__
// Fixed scratch locations in Emscripten's virtual MEMFS (transparent to QFile, same as any other
// path) used in place of interactive dialogs below -- see CreateSaveFile()/OpenSaveFile()/
// CloseFile() for why. Recognized by path in CloseFile(), which is the one place both a
// menu-triggered and a typed "save"/"restore" command's save/restore eventually pass through
// (Save::Writer/Save::Parser's destructors both call frontend->CloseFile()). Declared in
// mainwindow.h too: MainWindow::RestoreGameTriggered() is what actually populates the restore
// path, via the browser's async file picker, before Starlane::RestoreGame() synchronously reads
// it back through OpenSaveFile().
QString WasmSavePath() { return QDir::tempPath() + QStringLiteral("/starlane-save.sls"); }
QString WasmRestorePath() { return QDir::tempPath() + QStringLiteral("/starlane-restore.sls"); }
#endif

void *CreateSaveFile() {
#ifdef __EMSCRIPTEN__
	// No dialog: Game::Save() calls this synchronously (whether reached via the "Save Game" menu
	// action or the player typing "save") and needs a handle back immediately -- which
	// WebAssembly can't do through an interactive picker; there's no synchronous browser file
	// API. Write to a fixed, well-known path instead. CloseFile() below, once the write is
	// finished, is what actually offers the file to the player as a real download.
	auto file = new QFile(WasmSavePath());
	if (!file->open(QIODevice::WriteOnly)) {
		delete file;
		return nullptr;
	}
	return file;
#else
	const auto result = QFileDialog::getSaveFileName(theWin, "Select save file location", QString(), "Starlane Save File (*.sls)");
	if (result.isEmpty()) return nullptr;
	auto file = new QFile(result);
	if (!file->open(QIODevice::WriteOnly)) {
		delete file;
		return nullptr;
	}
	return file;
#endif
}

void *OpenSaveFile() {
#ifdef __EMSCRIPTEN__
	// No dialog, for the same reason as CreateSaveFile() above. Only MainWindow::
	// RestoreGameTriggered() (the "Restore Game" menu action) populates this path, by prompting
	// the browser's own file picker itself *before* calling Starlane::RestoreGame() -- so
	// restoring only works if that ran earlier in this session. A typed "restore" command with
	// nothing staged here finds no file, same as a cancelled dialog on native platforms
	// (Game::Restore() already handles that case: it just returns false).
	auto file = new QFile(WasmRestorePath());
#else
	const auto result = QFileDialog::getOpenFileName(theWin, "Select a save file", QString(), "Starlane Save File (*.sls)");
	if (result.isEmpty()) return nullptr;
	auto file = new QFile(result);
#endif
	if (!file->open(QIODevice::ReadOnly)) {
		delete file;
		return nullptr;
	}
	return file;
}

size_t ReadFile(void *hFile, uint8_t *buffer, size_t bufsize) {
	if (bufsize == 0) return 0;
	if constexpr (sizeof(bufsize) == sizeof(qint64)) {  //NOLINT
		if (bufsize > INT64_MAX)
			throw std::out_of_range("SlQt::ReadFile: too much data requested.");
	}
	auto *file = reinterpret_cast<QFile *>(hFile);
	return file->read(reinterpret_cast<char *>(buffer), (qint64) bufsize);
}

void WriteFile(void *hFile, const uint8_t *buffer, size_t count) {
	if (count == 0) return;
	if constexpr (sizeof(count) == sizeof(qint64)) {  //NOLINT
		// yes, for 64-bit platforms this condition is always true. that's why it's marked `constexpr`...
		// (not that there should ever be anywhere near this much data, anyways)
		if (count > INT64_MAX)
			throw std::out_of_range("SlQt::WriteFile: too much data.");
	}
	auto *file = reinterpret_cast<QFile *>(hFile);
	file->write(reinterpret_cast<const char *>(buffer), (qint64) count);
}

void CloseFile(void *hFile) {
	auto file = reinterpret_cast<QFile *>(hFile);
#ifdef __EMSCRIPTEN__
	// Every save and restore, however it was triggered, ends up here exactly once (Save::Writer/
	// Save::Parser's destructors both call this) -- the one place that's true regardless of
	// whether CreateSaveFile()/OpenSaveFile() above were reached via the menu actions or a typed
	// "save"/"restore" command, so it's where the WASM-specific follow-up for each happens.
	const QString path = file->fileName();
	file->close();
	if (path == WasmSavePath()) {
		// The save just finished writing, with no chance to ask the player where to put it up
		// front (see CreateSaveFile()) -- offer it as a real download now that it's complete.
		QFile finished(path);
		if (finished.open(QIODevice::ReadOnly))
			QFileDialog::saveFileContent(finished.readAll(), QStringLiteral("game.sls"), theWin);
	} else if (path == WasmRestorePath()) {
		// Consumed: remove it so a later bare "restore" command, with nothing freshly staged via
		// the menu action, reliably finds nothing instead of silently reusing this same file.
		QFile::remove(path);
	}
#else
	file->close();
#endif
	delete file;
}

// The ADRIFT 5 Runner always renders against a black background (its
// DEFAULT_BACKGROUNDCOLOUR is Color.Black -- see Global.vb), and every game author picks their
// InputColour/OutputColour with that assumption in mind -- ADRIFT's own defaults for those are a
// muted red and teal, both unreadable on a light background. Forcing a dark palette here,
// regardless of the desktop's own light/dark setting, is what keeps those colors looking the way
// their author intended rather than merely "as readable as they happen to be on white". Fusion
// (rather than the native style) is what makes a custom QPalette take effect consistently across
// platforms -- some native styles otherwise ignore several of these roles.
//
// Only the output pane (MainWindow's `output`, forced separately to pure black -- see its
// constructor) needs to actually be Color.Black; everywhere else just needs to look clearly
// distinct from it and from each other, so the window chrome, the input line, and the output pane
// don't all blur into one shapeless black rectangle. Window/Button/AlternateBase get the
// lightest shade (general chrome); Base -- the input line's background -- sits a step darker,
// between that and the output pane's true black.
void ApplyDarkTheme() {
	qApp->setStyle(QStyleFactory::create("Fusion"));
	QPalette pal;
	pal.setColor(QPalette::Window, QColor(53, 53, 53));
	pal.setColor(QPalette::WindowText, Qt::white);
	pal.setColor(QPalette::Base, QColor(25, 25, 25));
	pal.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
	pal.setColor(QPalette::ToolTipBase, Qt::white);
	pal.setColor(QPalette::ToolTipText, Qt::white);
	pal.setColor(QPalette::Text, Qt::white);
	pal.setColor(QPalette::Button, QColor(53, 53, 53));
	pal.setColor(QPalette::ButtonText, Qt::white);
	pal.setColor(QPalette::BrightText, Qt::red);
	pal.setColor(QPalette::Link, QColor(100, 180, 255));
	pal.setColor(QPalette::Highlight, QColor(45, 90, 140));
	pal.setColor(QPalette::HighlightedText, Qt::white);
	pal.setColor(QPalette::PlaceholderText, Qt::lightGray);
	pal.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
	pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
	pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
	qApp->setPalette(pal);
}

#ifdef __EMSCRIPTEN__
// Directly (and permanently) increments Emscripten's runtime-keepalive reference count -- see the
// long comment at its call site in main() below for why. A plain call into the generated JS
// runtime's own runtimeKeepalivePush(), rather than the documented emscripten_exit_with_live_
// runtime() API for this: that one is marked noreturn and, true to its name, unwinds the whole C++
// stack via a throw the moment it's called (the same mechanism Qt's own QCoreApplication::exec()
// apparently already uses to return control to the browser on WASM) -- so calling it after
// app.exec() never actually ran any code of ours, it just never returned to begin with.
EM_JS(void, KeepWasmRuntimeAliveForever, (), {
	runtimeKeepalivePush();
});
#endif

}

#include "../starlane-core/game.h"

int main(int argc, char **argv) {
	using namespace SlQt;

	StarlaneApplication app(argc, argv);
#ifdef __EMSCRIPTEN__
	// See KeepWasmRuntimeAliveForever()'s own comment. Called as early as possible so it's in
	// place before any of the dozens of one-shot startup callbacks that drain the runtime's
	// keepalive count get a chance to run.
	KeepWasmRuntimeAliveForever();
#endif
	// Needed for QSettings' default constructor (used to persist window geometry, see
	// MainWindow) to resolve a sensible, stable preferences location -- matching the reversed
	// form of MACOSX_BUNDLE_GUI_IDENTIFIER (CMakeLists.txt) so macOS's ~/Library/Preferences
	// entry lines up with the app's actual bundle identifier.
	QCoreApplication::setOrganizationDomain(QStringLiteral("diepixelecke.de"));
	QCoreApplication::setOrganizationName(QStringLiteral("Die Pixelecke"));
	QCoreApplication::setApplicationName(QStringLiteral("Starlane"));
	::setlocale(LC_ALL, ".utf-8");
	ApplyDarkTheme();
	Starlane::Frontend fe {
		/* .randomSeed = */ 0,
		/* .timersAvailable = */ true,  // MainWindow drives TimeTick once a second
		/* .FatalError = */ &FatalError,
		/* .OutputText = */ &OutputText,
		/* .StrToUpperCase = */ &StrToUpperCase,
		/* .StrToLowerCase = */ &StrToLowerCase,
		/* .StrToSentenceCase = */ &StrToSentenceCase,
		/* .AskYesNo = */ &AskYesNo,
		/* .QuitGame = */ &QuitGame,
		/* .PumpEvents = */ &PumpEvents,
		/* .CreateSaveFile = */ &CreateSaveFile,
		/* .OpenSaveFile = */ &OpenSaveFile,
		/* .ReadFile = */ &ReadFile,
		/* .WriteFile = */ &WriteFile,
		/* .CloseFile = */ &CloseFile
	};
	Starlane::InitBackend(&fe);
	theWin = new MainWindow;
	// After theWin exists (DebugEvent forwards to it), but before any game can load and start
	// producing debug events -- see DebugLogWindow::ApplyEnabledCategories for which categories
	// this actually enables, if any, at any given moment.
	Starlane::SetDebugEventCallback(&DebugEvent);
	theWin->show();

	// A file may have arrived as a QFileOpenEvent (macOS "open with"/double-click) before theWin
	// existed to handle it directly.
	const QString pendingOpenPath = app.TakePendingOpenPath();
	if (!pendingOpenPath.isEmpty())
		theWin->LoadGameFile(pendingOpenPath);
	else if (argc >= 2)  // Windows/Linux "open with": the game file arrives as a command-line argument
		theWin->LoadGameFile(QString::fromLocal8Bit(argv[1]));

	// LoadGameFile() above runs synchronously, all before app.exec() below has ever been called --
	// including its own QApplication::processEvents() calls (see LoadGameData()), which could in
	// principle process an already-queued close event. (A game's intro almost always ends in a
	// <waitkey>, but that no longer risks this the way it once did: OutputFormatter::AppendText()
	// just pauses and returns, rather than blocking in a nested event loop, so LoadGameFile()
	// itself isn't kept running by one waiting on the player.) If the window gets closed during
	// that window, MainWindow::closeEvent()'s qApp->quit() has nothing to actually terminate yet:
	// app.exec() hasn't started, so there's no event loop for the quit to reach. Entering it
	// anyway a moment later would start an unrelated, indefinitely-running session with no window
	// left to show for it (a wholly separate hazard from a close reaching MainWindow::
	// closeEvent() once the real main loop is already running, which the closeEvent() override
	// already handles fine).
	//
	// Checked via WasClosed() rather than isVisible(): on WebAssembly, show() doesn't necessarily
	// make isVisible() true synchronously (rendering/compositing there is asynchronous), so that
	// check was true on every WASM launch -- returning before app.exec() ever ran, at which point
	// tearing down the still-running pthread workers via the runtime's normal exit crashed the tab.
	if (theWin->WasClosed())
		return 0;

	return app.exec();
}
