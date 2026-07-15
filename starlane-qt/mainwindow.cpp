//
// Created by Adrian Welcker on 22.10.22.
//

#include "mainwindow.h"

#include <starlane-core.h>

MainWindow::MainWindow() : QMainWindow(nullptr) {
	auto *dummy = new QWidget(this);
	auto *box = new QVBoxLayout;

	output = new QTextBrowser;

	input = new QLineEdit;
	input->setPlaceholderText(">");
	connect(input, &QLineEdit::returnPressed, this, &MainWindow::InputReturnPressed);

	box->addWidget(output, 50);
	box->addWidget(input);
	dummy->setLayout(box);
	setCentralWidget(dummy);

	eventTimer = new QTimer(this);
	eventTimer->setInterval(1000);  // the core counts real-time events in whole seconds
	connect(eventTimer, &QTimer::timeout, this, []{ Starlane::TimeTick(); });
}

void MainWindow::StartEventTimer() {
	eventTimer->start();
}

void MainWindow::OutputText(const char *txt) {
	output->moveCursor(QTextCursor::End, QTextCursor::MoveAnchor);
	QString theText = QString(txt).replace('\n', "<br>")
	                                 .replace("<center>", "<div style=\"text-align: center;\">", Qt::CaseInsensitive)
	                                 .replace("</center>", "</div>", Qt::CaseInsensitive)
	                                 .replace("<centre>", "<div style=\"text-align: center;\">", Qt::CaseInsensitive)
	                                 .replace("</centre>", "</div>", Qt::CaseInsensitive);
	output->insertHtml(theText);
	output->ensureCursorVisible();
}

void MainWindow::InputReturnPressed() {
	output->insertHtml(QStringLiteral("<br><font color=red>> ") + input->text() + QStringLiteral("</font><br>"));
	std::string cmd(input->text().toStdString());
	input->clear();
	Starlane::ProcessInput(cmd);
}