//
// Created by Adrian Welcker on 22.10.22.
//

#include "mainwindow.h"

#include <starlane-core.h>

#include <QtCore/QCoreApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>

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

	formatter = new OutputFormatter(output, [this]{ WaitForKeyOrClick(); });

	eventTimer = new QTimer(this);
	eventTimer->setInterval(1000);  // the core counts real-time events in whole seconds
	connect(eventTimer, &QTimer::timeout, this, &MainWindow::HandleTimeTick);

	qApp->installEventFilter(this);
}

void MainWindow::ApplyGameInfo() {
	Starlane::GameInfo info;
	if (Starlane::GetGameInfo(&info)) {
		QString title = QString::fromUtf8(info.title.c_str());
		if (!info.author.empty())
			title += QStringLiteral(" by ") + QString::fromUtf8(info.author.c_str());
		setWindowTitle(title.isEmpty() ? QStringLiteral("Starlane") : title);
	}
	formatter->ApplyGameDefaults();
}

void MainWindow::StartEventTimer() {
	eventTimer->start();
}

void MainWindow::RunBeginGame() {
	formatter->BeginBatch();
	Starlane::BeginGame();
	formatter->EndBatch();
}

void MainWindow::HandleTimeTick() {
	formatter->BeginBatch();
	Starlane::TimeTick();
	formatter->EndBatch();
}

void MainWindow::OutputText(const char *txt) {
	formatter->AppendText(QString::fromUtf8(txt));
}

void MainWindow::InputReturnPressed() {
	formatter->BeginBatch();
	// The echoed command flows through the same tag parser as everything else, so escape
	// anything the player typed that would otherwise be misread as markup.
	QString escaped = input->text();
	escaped.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
	formatter->AppendText(QStringLiteral("<br><font color=\"") + OutputFormatter::CommandColor().name()
		+ QStringLiteral("\">> ") + escaped + QStringLiteral("</font><br>"));

	std::string cmd(input->text().toStdString());
	input->clear();
	Starlane::ProcessInput(cmd);
	formatter->EndBatch();
}

void MainWindow::WaitForKeyOrClick() {
	input->setEnabled(false);
	QEventLoop loop;
	waitKeyLoop = &loop;
	loop.exec();
	waitKeyLoop = nullptr;
	input->setEnabled(true);
	input->setFocus();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
	if (waitKeyLoop && (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)) {
		waitKeyLoop->quit();
		return true;  // consume: don't let this key/click also reach whatever widget it landed on
	}
	return QMainWindow::eventFilter(watched, event);
}