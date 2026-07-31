//
// Created by Adrian Welcker on 22.10.22.
//

#include "mainwindow.h"

#include <starlane-core.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QTextStream>
#include <QtGui/QCloseEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPalette>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>

namespace {
constexpr auto kGeometrySettingsKey = "mainWindowGeometry";
}  // namespace

MainWindow::MainWindow() : QMainWindow(nullptr) {
	setWindowTitle(QStringLiteral("Starlane"));

	// Restore the window size/position the player left it at; failing that (first launch, or
	// nothing usable saved), size it relative to the available screen instead of some fixed pixel
	// size -- what looks reasonable on a laptop display would be tiny on a 4K monitor, and vice
	// versa. closeEvent() is where this gets saved back.
	const QByteArray savedGeometry = QSettings().value(kGeometrySettingsKey).toByteArray();
	if (savedGeometry.isEmpty() || !restoreGeometry(savedGeometry)) {
		const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
		resize(avail.width() / 2, avail.height() * 2 / 3);
		move(avail.center() - rect().center());
	}

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

	formatter = new OutputFormatter(output, [this]{ if (!isReplaying) WaitForKeyOrClick(); },
		[this](const QString &path){ return LoadImage(path); });

	eventTimer = new QTimer(this);
	eventTimer->setInterval(1000);  // the core counts real-time events in whole seconds
	connect(eventTimer, &QTimer::timeout, this, &MainWindow::HandleTimeTick);

	qApp->installEventFilter(this);

	// StatusBarPanel (the WinForms control the original ADRIFT Runner uses for these) has no rich
	// text support, and the Runner's own UpdateStatusBar() assigns location/userStatus to it
	// verbatim -- so, matching that, these show Starlane::StatusBar's fields as plain text rather
	// than running them through OutputFormatter's tag parsing.
	locationLabel = new QLabel;
	locationLabel->setTextFormat(Qt::PlainText);
	userStatusLabel = new QLabel;
	userStatusLabel->setTextFormat(Qt::PlainText);
	scoreLabel = new QLabel;
	scoreLabel->setTextFormat(Qt::PlainText);
	scoreLabel->hide();  // no score segment until a game says it uses scoring
	statusBar()->addWidget(locationLabel);
	statusBar()->addWidget(userStatusLabel, 1);
	statusBar()->addPermanentWidget(scoreLabel);

	CreateMenus();
	UpdateActionState();
	UpdateStatusBar();

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

void MainWindow::UpdateStatusBar() {
	Starlane::StatusBar sb;
	if (!Starlane::GetStatusBar(sb)) {
		locationLabel->clear();
		userStatusLabel->clear();
		scoreLabel->hide();
		return;
	}
	locationLabel->setText(QString::fromUtf8(sb.location.c_str()));
	userStatusLabel->setText(QString::fromUtf8(sb.userStatus.c_str()));
	if (sb.scoringUsed) {
		scoreLabel->setText(tr("Score: %1").arg(sb.score));
		scoreLabel->show();
	} else {
		scoreLabel->hide();
	}
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
	UpdateStatusBar();
}

void MainWindow::HandleTimeTick() {
	formatter->BeginBatch();
	Starlane::TimeTick();
	formatter->EndBatch();
	UpdateActionState();
	UpdateStatusBar();
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
	QByteArray data = file.readAll();
	file.close();

	// ADRIFT games are commonly distributed as a Blorb archive bundling the game itself (as the
	// "Exec" resource) together with its images and sounds -- extract the game from there rather
	// than trying to load the whole archive as if it were a bare .taf, and keep the parsed archive
	// around afterwards so LoadImage() can pull <img>-referenced Pict resources out of it later.
	std::optional<BlorbFile> blorb;
	if (BlorbFile::IsBlorbData(data)) {
		blorb = BlorbFile::Parse(data);
		if (!blorb) {
			QMessageBox::warning(this, QStringLiteral("Starlane"),
				tr("\"%1\" does not appear to be a valid Blorb file.").arg(QDir::toNativeSeparators(path)));
			return false;
		}
		data = blorb->GetExecResource();
		if (data.isEmpty()) {
			QMessageBox::warning(this, QStringLiteral("Starlane"),
				tr("\"%1\" is a Blorb file, but it doesn't contain a game -- it can only be used "
				   "alongside a separate .taf file.").arg(QDir::toNativeSeparators(path)));
			return false;
		}
	}
	currentBlorb = std::move(blorb);

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

QImage MainWindow::LoadImage(const QString &path) const {
	if (currentBlorb) {
		const uint32_t resourceId = Starlane::GetBlorbResourceForPath(path.toStdString());
		if (resourceId == (uint32_t) -1) return QImage();
		auto resource = currentBlorb->GetResource(BlorbFile::kUsagePict, resourceId);
		if (!resource) return QImage();
		return QImage::fromData(resource->data);
	}

	// No Blorb: match the original Runner's own behavior for a non-Blorb game (Global.vb's
	// Source2HTML hands the <img src> value straight to PictureBox.Load with no resolution
	// against the game file's own location) by trying the path exactly as given, rather than
	// inventing a resolution scheme of our own that it never had. Backslashes are swapped for
	// forward slashes since these paths are almost always authored on Windows (see how a game's
	// FileMappings entries look -- e.g. "C:\ADRIFT Images\Cover Images\Foo.jpg") and Qt, unlike
	// the OS the path was authored on, doesn't accept them as separators.
	QString resolved = path;
	resolved.replace('\\', '/');
	return QImage(resolved);
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
	UpdateStatusBar();
}

void MainWindow::InputReturnPressed() {
	const QString cmd = input->text();
	input->clear();
	SubmitCommand(cmd);
}

void MainWindow::OpenGameTriggered() {
	const QString path = QFileDialog::getOpenFileName(this, tr("Open Game"), QString(),
		tr("ADRIFT Game Files (*.taf *.blorb *.adriftblorb)"));
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
	UpdateStatusBar();
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

void MainWindow::closeEvent(QCloseEvent *event) {
	// A <waitkey> tag blocks in WaitForKeyOrClick()'s own nested QEventLoop. Closing the window
	// while that's running should release it -- otherwise it just sits there blocked on input
	// that will never come once the window (and, via quitOnLastWindowClosed below, soon the
	// whole app) is gone. See main() in starlane.cpp for the closely related hazard of a game's
	// *initial* <waitkey> (almost every game has one) getting closed before app.exec() has even
	// been reached yet.
	if (waitKeyLoop) waitKeyLoop->quit();
	QSettings().setValue(kGeometrySettingsKey, saveGeometry());
	QMainWindow::closeEvent(event);
	qApp->quit();
}
