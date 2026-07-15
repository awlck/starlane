//
// Created by Adrian Welcker on 22.10.22.
//

#ifndef STARLANE_MAINWINDOW_H
#define STARLANE_MAINWINDOW_H

#include <QtCore/QTimer>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTextBrowser>

class MainWindow: public QMainWindow {
	Q_OBJECT
public:
	MainWindow();

	void OutputText(const char *txt);
	// Start the once-a-second clock that drives the core's real-time events. Call once the game
	// has begun; there is nothing for it to advance before that.
	void StartEventTimer();
private:
	QTextBrowser *output;
	QLineEdit *input;
	// Real-time events run on wall-clock seconds, so the core wants a tick a second. It ignores
	// ticks arriving before the game starts or part-way through a command, so this can simply run
	// for the life of the window -- which is what ADRIFT does with its own.
	QTimer *eventTimer;

	void InputReturnPressed();
};

#endif  // !STARLANE_MAINWINDOW_H
