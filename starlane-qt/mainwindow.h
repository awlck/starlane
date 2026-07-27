//
// Created by Adrian Welcker on 22.10.22.
//

#ifndef STARLANE_MAINWINDOW_H
#define STARLANE_MAINWINDOW_H

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
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

	// Set while a <waitkey> tag is blocking on WaitForKeyOrClick(), so eventFilter() knows to
	// consume the next key/click instead of letting it reach whatever widget it landed on.
	QEventLoop *waitKeyLoop = nullptr;

	void InputReturnPressed();
	void HandleTimeTick();
	// Blocks (via a nested event loop) until the next keypress or mouse click anywhere in the
	// app. Used to implement the <waitkey> tag.
	void WaitForKeyOrClick();
};

#endif  // !STARLANE_MAINWINDOW_H
