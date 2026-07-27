//
// Created by Adrian Welcker on 22.10.22.
//

#ifndef STARLANE_MAINWINDOW_H
#define STARLANE_MAINWINDOW_H

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTextBrowser>

#include "outputformatter.h"

class MainWindow: public QMainWindow {
	Q_OBJECT
public:
	MainWindow();

	void OutputText(const char *txt);
	// Applies the game's bibliographic/display info (window title, default font/colors) once it is
	// known. Call after CreateGame() and before RunBeginGame().
	void ApplyGameInfo();
	// Start the once-a-second clock that drives the core's real-time events. Call once the game
	// has begun; there is nothing for it to advance before that.
	void StartEventTimer();
	// Wraps Starlane::BeginGame() with the output-batch bookkeeping (see OutputFormatter).
	// Call this instead of Starlane::BeginGame() directly.
	void RunBeginGame();

	// Loads the TAF file at `path` as a new game, replacing whatever game is currently loaded
	// (asking for confirmation first if one is ongoing). Used by the "Open Game" menu action, a
	// command-line argument, and OS "open with"/double-click delivery alike.
	bool LoadGameFile(const QString &path);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QTextBrowser *output;
	QLineEdit *input;
	// Real-time events run on wall-clock seconds, so the core wants a tick a second. It ignores
	// ticks arriving before the game starts or part-way through a command, so this can simply run
	// for the life of the window -- which is what ADRIFT does with its own.
	QTimer *eventTimer;
	OutputFormatter *formatter;

	QAction *openGameAction;
	QAction *saveGameAction;
	QAction *restoreGameAction;
	QAction *transcriptAction;
	QAction *replayAction;
	bool transcribing = false;

	// Set while ReplayCommandsTriggered() is feeding commands from a file, so the <waitkey>
	// handler knows not to block the replay on player input that isn't coming.
	bool isReplaying = false;

	// Set while a <waitkey> tag is blocking on WaitForKeyOrClick(), so eventFilter() knows to
	// consume the next key/click instead of letting it reach whatever widget it landed on.
	QEventLoop *waitKeyLoop = nullptr;

	void CreateMenus();
	// Enables/disables the game-dependent menu actions based on Starlane::GameIsOngoing().
	void UpdateActionState();

	// Sends `cmd` to the game as if the player had typed it: echoes it to the output, then runs
	// it through Starlane::ProcessInput() with the usual output-batch bookkeeping.
	void SubmitCommand(const QString &cmd);

	void InputReturnPressed();
	void HandleTimeTick();
	void OpenGameTriggered();
	void SaveGameTriggered();
	void RestoreGameTriggered();
	void ToggleTranscript();
	void ReplayCommandsTriggered();
	// Blocks (via a nested event loop) until the next keypress or mouse click anywhere in the
	// app. Used to implement the <waitkey> tag.
	void WaitForKeyOrClick();
};

#endif  // !STARLANE_MAINWINDOW_H
