//
// Created by Adrian Welcker on 22.10.22.
//

#include "mainwindow.h"

#include <starlane-core.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>

MainWindow::MainWindow() : QMainWindow(nullptr) {
	setWindowTitle(QStringLiteral("Starlane"));

	auto *dummy = new QWidget(this);
	auto *box = new QVBoxLayout;

	output = new QTextBrowser;
	// Pure black, deliberately darker than the rest of the app's dark theme (see ApplyDarkTheme()
	// in starlane.cpp) -- ADRIFT authors pick their InputColour/OutputColour assuming a true-black
	// background, not merely "a dark one", and the contrast also helps set the output pane apart
	// from the input line and window chrome around it.
	QPalette outputPalette = output->palette();
	outputPalette.setColor(QPalette::Base, Qt::black);
	output->setPalette(outputPalette);

	input = new QLineEdit;
	input->setPlaceholderText(">");
	connect(input, &QLineEdit::returnPressed, this, &MainWindow::InputReturnPressed);

	box->addWidget(output, 50);
	box->addWidget(input);
	dummy->setLayout(box);
	setCentralWidget(dummy);

	formatter = new OutputFormatter(output, [this]{ if (!isReplaying) WaitForKeyOrClick(); });

	eventTimer = new QTimer(this);
	eventTimer->setInterval(1000);  // the core counts real-time events in whole seconds
	connect(eventTimer, &QTimer::timeout, this, &MainWindow::HandleTimeTick);

	qApp->installEventFilter(this);

	CreateMenus();
	UpdateActionState();

	formatter->BeginBatch();
	formatter->AppendText(QStringLiteral("Welcome to Starlane. Use <b>File → Open Game...</b> to load a game."));
	formatter->EndBatch();
}

void MainWindow::CreateMenus() {
	auto *fileMenu = menuBar()->addMenu(tr("&File"));
	openGameAction = fileMenu->addAction(tr("&Open Game..."), QKeySequence::Open, this, &MainWindow::OpenGameTriggered);

	auto *gameMenu = menuBar()->addMenu(tr("&Game"));
	saveGameAction = gameMenu->addAction(tr("&Save Game"), QKeySequence::Save, this, &MainWindow::SaveGameTriggered);
	restoreGameAction = gameMenu->addAction(tr("&Restore Game..."), QKeySequence(Qt::CTRL | Qt::Key_R), this, &MainWindow::RestoreGameTriggered);
	gameMenu->addSeparator();
	transcriptAction = gameMenu->addAction(tr("Start &Transcript"), this, &MainWindow::ToggleTranscript);
	replayAction = gameMenu->addAction(tr("Repla&y Commands..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this, &MainWindow::ReplayCommandsTriggered);
}

void MainWindow::UpdateActionState() {
	const bool ongoing = Starlane::GameIsOngoing();
	saveGameAction->setEnabled(ongoing);
	restoreGameAction->setEnabled(ongoing);
	transcriptAction->setEnabled(ongoing);
	replayAction->setEnabled(ongoing && !isReplaying);
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
	UpdateActionState();
}

void MainWindow::HandleTimeTick() {
	formatter->BeginBatch();
	Starlane::TimeTick();
	formatter->EndBatch();
	UpdateActionState();
}

void MainWindow::OutputText(const char *txt) {
	formatter->AppendText(QString::fromUtf8(txt));
}

bool MainWindow::LoadGameFile(const QString &path) {
	if (path.isEmpty()) return false;

	if (Starlane::GameIsOngoing()) {
		auto result = QMessageBox::question(this, QStringLiteral("Starlane"),
			tr("Loading a new game will discard the current one. Continue?"),
			QMessageBox::Yes | QMessageBox::No);
		if (result != QMessageBox::Yes) return false;
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, QStringLiteral("Starlane"),
			tr("Could not open \"%1\".").arg(QDir::toNativeSeparators(path)));
		return false;
	}
	const QByteArray data = file.readAll();
	file.close();

	eventTimer->stop();
	output->clear();
	setWindowTitle(QStringLiteral("Starlane"));
	QApplication::processEvents();  // let the cleared window paint before the (synchronous) load below

	Starlane::CreateGame(reinterpret_cast<const uint8_t *>(data.constData()), (size_t) data.size());
	ApplyGameInfo();
	QApplication::processEvents();
	RunBeginGame();
	StartEventTimer();
	input->setFocus();
	return Starlane::GameIsOngoing();
}

void MainWindow::SubmitCommand(const QString &cmd) {
	formatter->BeginBatch();
	// The echoed command flows through the same tag parser as everything else, so escape
	// anything the player typed that would otherwise be misread as markup.
	QString escaped = cmd;
	escaped.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
	formatter->AppendText(QStringLiteral("<br><font color=\"") + OutputFormatter::CommandColor().name()
		+ QStringLiteral("\">> ") + escaped + QStringLiteral("</font><br>"));

	Starlane::ProcessInput(cmd.toStdString());
	formatter->EndBatch();
	UpdateActionState();
}

void MainWindow::InputReturnPressed() {
	const QString cmd = input->text();
	input->clear();
	SubmitCommand(cmd);
}

void MainWindow::OpenGameTriggered() {
	const QString path = QFileDialog::getOpenFileName(this, tr("Open Game"), QString(),
		tr("ADRIFT Game Files (*.taf)"));
	if (!path.isEmpty()) LoadGameFile(path);
}

void MainWindow::SaveGameTriggered() {
	formatter->BeginBatch();
	Starlane::SaveGame();
	formatter->EndBatch();
}

void MainWindow::RestoreGameTriggered() {
	formatter->BeginBatch();
	Starlane::RestoreGame();
	formatter->EndBatch();
	UpdateActionState();
}

void MainWindow::ToggleTranscript() {
	// No-op for now: just flips the menu label. Actual transcript writing is a follow-up.
	transcribing = !transcribing;
	transcriptAction->setText(transcribing ? tr("Stop &Transcript") : tr("Start &Transcript"));
}

void MainWindow::ReplayCommandsTriggered() {
	const QString path = QFileDialog::getOpenFileName(this, tr("Replay Commands"), QString(),
		tr("Command Files (*.txt)"));
	if (path.isEmpty()) return;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QMessageBox::warning(this, QStringLiteral("Starlane"),
			tr("Could not open \"%1\".").arg(QDir::toNativeSeparators(path)));
		return;
	}

	isReplaying = true;
	input->setEnabled(false);
	UpdateActionState();

	QTextStream in(&file);
	while (!in.atEnd() && Starlane::GameIsOngoing()) {
		const QString line = in.readLine();
		SubmitCommand(line);
		QApplication::processEvents();
	}

	isReplaying = false;
	input->setEnabled(true);
	input->setFocus();
	UpdateActionState();
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