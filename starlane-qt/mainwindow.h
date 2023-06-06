//
// Created by Adrian Welcker on 22.10.22.
//

#ifndef STARLANE_MAINWINDOW_H
#define STARLANE_MAINWINDOW_H

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTextBrowser>

class MainWindow: public QMainWindow {
	Q_OBJECT
public:
	MainWindow();

	void OutputText(const char *txt);
private:
	QTextBrowser *output;
	QLineEdit *input;

	void InputReturnPressed();
};

#endif  // !STARLANE_MAINWINDOW_H
