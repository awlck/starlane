#include <QtCore/QElapsedTimer>
#include <QtCore/QProcess>
#include <QtCore/QString>
#include <QtWidgets/QApplication>

#include <starlane-core.h>
#include <clocale>
#include <QtGui/QFileOpenEvent>
#include <QtGui/QPalette>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyleFactory>

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
				QProcess::startDetached(QCoreApplication::applicationFilePath(), {path});
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

std::string StrToLowerCase(const std::string &s) {
	QString lower = QString::fromUtf8(s.c_str(), s.length()).toLower();
	return lower.toUtf8().toStdString();
}

std::string StrToUpperCase(const std::string &s) {
	return QString::fromUtf8(s.c_str(), s.length()).toUpper().toUtf8().toStdString();
}

std::string StrToSentenceCase(const std::string &s) {  // TODO
	return QString::fromUtf8(s.c_str(), s.length()).toUpper().toUtf8().toStdString();
}

bool AskYesNo(const char *question) {
	return QMessageBox::question(theWin, "Starlane", QString::fromUtf8(question),
	                             QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

void QuitGame() {
	QApplication::quit();
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

void *CreateSaveFile() {
	const auto result = QFileDialog::getSaveFileName(theWin, "Select save file location", QString(), "Starlane Save File (*.sls)");
	if (result.isEmpty()) return nullptr;
	auto file = new QFile(result);
	if (!file->open(QIODevice::WriteOnly)) {
		delete file;
		return nullptr;
	}
	return file;
}

void *OpenSaveFile() {
	const auto result = QFileDialog::getOpenFileName(theWin, "Select a save file", QString(), "Starlane Save File (*.sls)");
	if (result.isEmpty()) return nullptr;
	auto file = new QFile(result);
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
	file->close();
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

}

#include "../starlane-core/game.h"

int main(int argc, char **argv) {
	using namespace SlQt;

	StarlaneApplication app(argc, argv);
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
	theWin->show();

	// A file may have arrived as a QFileOpenEvent (macOS "open with"/double-click) before theWin
	// existed to handle it directly.
	const QString pendingOpenPath = app.TakePendingOpenPath();
	if (!pendingOpenPath.isEmpty())
		theWin->LoadGameFile(pendingOpenPath);
	else if (argc >= 2)  // Windows/Linux "open with": the game file arrives as a command-line argument
		theWin->LoadGameFile(QString::fromLocal8Bit(argv[1]));

	// LoadGameFile() above runs synchronously and can itself pump nested event loops (a game's
	// intro almost always ends in a <waitkey>, which blocks in MainWindow::WaitForKeyOrClick()'s
	// own QEventLoop) -- all before app.exec() below has ever been called. If the window gets
	// closed during that window, MainWindow::closeEvent()'s qApp->quit() has nothing to actually
	// terminate yet: it unwinds the nested loop that was running at the time, but app.exec()
	// hasn't started, so there's no outer loop for the quit to reach. Entering it anyway a moment
	// later would start an unrelated, indefinitely-running session with no window left to show
	// for it (a wholly separate hazard from a close reaching MainWindow::closeEvent() once the
	// real main loop is already running, which the closeEvent() override already handles fine).
	if (!theWin->isVisible())
		return 0;

	return app.exec();
}
