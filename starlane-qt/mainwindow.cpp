//
// Created by Adrian Welcker on 22.10.22.
//

#include "mainwindow.h"

#include <starlane-core.h>
#include <starlane-version.h>

#include <QtCore/QBuffer>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QTextStream>
#include <QtCore/QUrl>
#include <QtGui/QCloseEvent>
#include <QtGui/QDesktopServices>
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

	formatter = new OutputFormatter(output, [this](OutputFormatter *fmt){ if (!isReplaying) OnWaitKey(fmt); },
		[this](const QString &path){ return LoadImage(path); },
		[this](const QString &src, int channel, bool loop){ PlaySound(src, channel, loop); },
		[this](int channel){ PauseSound(channel); },
		[this](int channel){ StopSound(channel); },
		[this](const QString &name){ return GetOrCreateSecondaryWindow(name); });

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

	// Created once, up front (unlike per-game secondary windows -- see ClearSecondaryWindows()),
	// since a debug event (e.g. a GameLoad one) can fire while a game is still loading, before
	// there's any game session for a window to belong to. Docked but hidden: opening it is what
	// enables its checked categories with starlane-core in the first place (see
	// DebugLogWindow::ApplyEnabledCategories), so leaving it hidden by default costs nothing.
	debugLogWindow = new DebugLogWindow(this);
	addDockWidget(Qt::BottomDockWidgetArea, debugLogWindow);
	debugLogWindow->hide();

	CreateMenus();
	UpdateActionState();
	UpdateStatusBar();

	BeginOutputBatch();
	formatter->AppendText(QStringLiteral("Welcome to Starlane. Use <b>File → Open Game...</b> to load a game."));
	EndOutputBatch();
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

	// Populated as the game opens secondary windows (see GetOrCreateSecondaryWindow()) -- empty,
	// and therefore unremarkable, for games that don't use <window> markup at all.
	windowsMenu = menuBar()->addMenu(tr("&Windows"));

	debugLogAction = debugLogWindow->toggleViewAction();
	debugLogAction->setText(tr("&Debug Log"));
	windowsMenu->addAction(debugLogAction);

	auto *helpMenu = menuBar()->addMenu(tr("Help"));
	helpMenu->setToolTipsVisible(true);
	auto *checkForUpdatesAction = helpMenu->addAction(tr("Check for updates..."));
	checkForUpdatesAction->setToolTip(tr("Open your default browser to check GitHub for new releases."));
	connect(checkForUpdatesAction, &QAction::triggered, this, [this] {
		bool ok = QDesktopServices::openUrl(tr("https://github.com/awlck/starlane/releases"));
		if (!ok) {
			QMessageBox::warning(this, tr("Update Check Failed"), tr("An error occurred, and I was unable to open "
			"the releases page in any browser."));
		}
	});
	auto *aboutQtAction = helpMenu->addAction(tr("About Qt"));
	aboutQtAction->setMenuRole(QAction::AboutQtRole);
	connect(aboutQtAction, &QAction::triggered, this, [this]{ QMessageBox::aboutQt(this); });
	auto *aboutSlAction = helpMenu->addAction(tr("About Starlane"));
	aboutSlAction->setMenuRole(QAction::AboutRole);
	connect(aboutSlAction, &QAction::triggered, this, [this] {
		QString version;
		if (QString(Starlane::Version) == QStringLiteral("0.0.0"))
			version = QStringLiteral("<development build>");
		else
			version = Starlane::Version;
		QMessageBox::about(this, tr("About Starlane"),
			tr("Starlane: a reimplementation of the ADRIFT 5 engine.\n\n"
			       "Version: %1\n"
			       "Copyright (c) 2022-26 Adrian Welcker, licensed under the Apache License 2.0\n"
			       "Check out the source and contribute at https://github.com/awlck/starlane").arg(version));
	});
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
	if (Starlane::GetGameInfo(info)) {
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
	BeginOutputBatch();
	Starlane::BeginGame();
	EndOutputBatch();
	UpdateActionState();
	UpdateStatusBar();
}

void MainWindow::HandleTimeTick() {
	BeginOutputBatch();
	Starlane::TimeTick();
	EndOutputBatch();
	UpdateActionState();
	UpdateStatusBar();
}

void MainWindow::OutputText(const char *txt) {
	formatter->AppendText(QString::fromUtf8(txt));
}

void MainWindow::OnDebugEvent(Starlane::DebugCategory category, const char *message) {
	debugLogWindow->AppendEvent(category, QString::fromUtf8(message));
}

OutputFormatter *MainWindow::GetOrCreateSecondaryWindow(const QString &name) {
	auto it = secondaryWindows.find(name);
	if (it != secondaryWindows.end()) return it->formatter;

	auto *browser = new QTextBrowser;
	// Same rationale as the main output pane's palette (see the constructor): a true-black
	// background matches what ADRIFT authors assume when picking OutputColour/InputColour.
	QPalette palette = browser->palette();
	palette.setColor(QPalette::Base, Qt::black);
	browser->setPalette(palette);

	auto *dock = new QDockWidget(name, this);
	dock->setObjectName(QStringLiteral("secondaryWindow_") + name);
	dock->setWidget(browser);
	dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
		| QDockWidget::DockWidgetFloatable);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);

	auto *secondaryFormatter = new OutputFormatter(browser,
		[this](OutputFormatter *fmt){ if (!isReplaying) OnWaitKey(fmt); },
		[this](const QString &path){ return LoadImage(path); },
		[this](const QString &src, int channel, bool loop){ PlaySound(src, channel, loop); },
		[this](int channel){ PauseSound(channel); },
		[this](int channel){ StopSound(channel); },
		[this](const QString &n){ return GetOrCreateSecondaryWindow(n); });

	// Tabify onto an existing secondary window rather than letting them pile up as separate splits
	// -- ADRIFT authors are cautioned against using more than one or two of these at once anyway.
	if (secondaryWindows.isEmpty())
		addDockWidget(Qt::RightDockWidgetArea, dock);
	else
		tabifyDockWidget(secondaryWindows.first().dock, dock);
	dock->show();
	windowsMenu->addAction(dock->toggleViewAction());

	// A batch already in progress when this window is first created (the usual case -- it's
	// created on demand while redirecting a <window NAME> tag's content, itself found partway
	// through the main formatter's own AppendText()) has already called BeginOutputBatch(), which
	// predates this window's existence and so couldn't have started its batch too. Do that now;
	// EndOutputBatch() will find it in secondaryWindows and close the batch out normally.
	secondaryFormatter->BeginBatch();

	secondaryWindows.insert(name, {dock, browser, secondaryFormatter});
	return secondaryFormatter;
}

void MainWindow::ClearSecondaryWindows() {
	for (auto it = secondaryWindows.begin(); it != secondaryWindows.end(); ++it) {
		removeDockWidget(it->dock);
		delete it->formatter;
		delete it->dock;  // also deletes its QTextBrowser, which is a child widget
	}
	secondaryWindows.clear();
	windowsMenu->clear();
	windowsMenu->addAction(debugLogAction);
}

void MainWindow::BeginOutputBatch() {
	formatter->BeginBatch();
	for (auto &window : secondaryWindows) window.formatter->BeginBatch();
}

void MainWindow::EndOutputBatch() {
	formatter->EndBatch();
	for (auto &window : secondaryWindows) window.formatter->EndBatch();
}

bool MainWindow::LoadGameFile(const QString &path) {
	if (path.isEmpty()) return false;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, QStringLiteral("Starlane"),
			tr("Could not open \"%1\".").arg(QDir::toNativeSeparators(path)));
		return false;
	}
	QByteArray data = file.readAll();
	file.close();

	return LoadGameData(path, std::move(data));
}

bool MainWindow::LoadGameData(const QString &displayName, QByteArray data) {
	if (Starlane::GameIsOngoing()) {
		auto result = QMessageBox::question(this, QStringLiteral("Starlane"),
			tr("Loading a new game will discard the current one. Continue?"),
			QMessageBox::Yes | QMessageBox::No);
		if (result != QMessageBox::Yes) return false;
	}

	// ADRIFT games are commonly distributed as a Blorb archive bundling the game itself (as the
	// "Exec" resource) together with its images and sounds -- extract the game from there rather
	// than trying to load the whole archive as if it were a bare .taf, and keep the parsed archive
	// around afterwards so LoadImage() can pull <img>-referenced Pict resources out of it later.
	std::optional<BlorbFile> blorb;
	if (BlorbFile::IsBlorbData(data)) {
		blorb = BlorbFile::Parse(data);
		if (!blorb) {
			QMessageBox::warning(this, QStringLiteral("Starlane"),
				tr("\"%1\" does not appear to be a valid Blorb file.").arg(QDir::toNativeSeparators(displayName)));
			return false;
		}
		data = blorb->GetExecResource();
		if (data.isEmpty()) {
			QMessageBox::warning(this, QStringLiteral("Starlane"),
				tr("\"%1\" is a Blorb file, but it doesn't contain a game -- it can only be used "
				   "alongside a separate .taf file.").arg(QDir::toNativeSeparators(displayName)));
			return false;
		}
	}
	currentBlorb = std::move(blorb);
	StopAllSounds();  // the previous game's audio (if any) shouldn't keep playing over the new one
	StopTranscript();  // a transcript is scoped to one game session, not the whole app lifetime
	ClearSecondaryWindows();  // likewise, secondary windows' content belongs to the previous game

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

#ifndef __EMSCRIPTEN__
void MainWindow::EnsureSoundChannel(SoundChannel &ch) {
	if (ch.player) return;
	ch.player = new QMediaPlayer(this);
	ch.output = new QAudioOutput(this);
	ch.player->setAudioOutput(ch.output);
}
#endif

void MainWindow::StopAllSounds() {
	for (int i = 1; i <= 8; i++) StopSound(i);
}

void MainWindow::PlaySound(const QString &src, int channel, bool loop) {
#ifndef __EMSCRIPTEN__
	SoundChannel &ch = soundChannels[channel];
	EnsureSoundChannel(ch);
	if (ch.recentlyPlayedSrc == src) {
		// Already the current sound on this channel: resume rather than restart from the top,
		// same as the Glk frontend's PlaySound() (multimedia.cpp).
		ch.player->play();
		return;
	}

	ch.player->stop();
	delete ch.buffer;
	ch.buffer = nullptr;

	if (currentBlorb) {
		const uint32_t resourceId = Starlane::GetBlorbResourceForPath(src.toStdString());
		if (resourceId == (uint32_t) -1) return;
		auto resource = currentBlorb->GetResource(BlorbFile::kUsageSnd, resourceId);
		if (!resource) return;
		// setSourceDevice() doesn't take ownership or copy the device, so the buffer backing it
		// has to outlive playback -- kept alive here as long as this channel's sound doesn't
		// change again (see the `delete ch.buffer` above, for when it does).
		ch.buffer = new QBuffer(this);
		ch.buffer->setData(resource->data);
		ch.buffer->open(QIODevice::ReadOnly);
		ch.player->setSourceDevice(ch.buffer);
	} else {
		// No Blorb: same rationale as LoadImage() for a non-Blorb <img> -- try the path exactly as
		// given (only swapping backslashes for forward slashes), rather than a resolution scheme
		// the original Runner never had.
		QString resolved = src;
		resolved.replace('\\', '/');
		ch.player->setSource(QUrl::fromLocalFile(resolved));
	}

	ch.player->setLoops(loop ? QMediaPlayer::Infinite : QMediaPlayer::Once);
	ch.player->play();
	ch.recentlyPlayedSrc = src;
#endif
}

void MainWindow::PauseSound(int channel) {
#ifndef __EMSCRIPTEN__
	SoundChannel &ch = soundChannels[channel];
	if (ch.player) ch.player->pause();
#endif
}

void MainWindow::StopSound(int channel) {
#ifndef __EMSCRIPTEN__
	SoundChannel &ch = soundChannels[channel];
	if (ch.player) ch.player->stop();
	ch.recentlyPlayedSrc.clear();
#endif
}

void MainWindow::SubmitCommand(const QString &cmd) {
	BeginOutputBatch();
	// The echoed command flows through the same tag parser as everything else, so escape
	// anything the player typed that would otherwise be misread as markup.
	QString escaped = cmd;
	escaped.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
	formatter->AppendText(QStringLiteral("<br><font color=\"") + OutputFormatter::CommandColor().name()
		+ QStringLiteral("\">> ") + escaped + QStringLiteral("</font><br>"));

	Starlane::ProcessInput(cmd.toStdString());
	EndOutputBatch();
	UpdateActionState();
	UpdateStatusBar();
}

void MainWindow::InputReturnPressed() {
	const QString cmd = input->text();
	input->clear();
	SubmitCommand(cmd);
}

void MainWindow::OpenGameTriggered() {
	// QFileDialog::getOpenFileName() shows a modal dialog via QDialog::exec() -- a nested event loop,
	// which isn't supported on WebAssembly's real browser main thread. getOpenFileContent() is Qt's
	// WebAssembly-specific replacement: it drives the browser's native <input type="file"> picker
	// asynchronously instead of blocking, and hands back the picked file's content directly rather
	// than a path -- WebAssembly has no real filesystem for a path to mean anything outside the app's
	// own virtual one.
	// For convenience, on desktop platforms, this just calls `QFileDialog::getOpenFileName()`,
	// reads in the file specified, and calls the callback, so there is no need for us to maintain
	// separate code paths here.
	QFileDialog::getOpenFileContent(tr("ADRIFT Game Files (*.taf *.blorb *.adriftblorb)"),
		[this](const QString &fileName, const QByteArray &fileContent) {
			if (fileName.isEmpty()) return;  // dialog was cancelled
			LoadGameData(fileName, fileContent);
		});
}

void MainWindow::SaveGameTriggered() {
	BeginOutputBatch();
	Starlane::SaveGame();
	EndOutputBatch();
}

void MainWindow::RestoreGameTriggered() {
#ifdef __EMSCRIPTEN__
	// Starlane::RestoreGame() -> Game::Restore() calls SlQt::OpenSaveFile() (starlane.cpp)
	// synchronously and needs a file already sitting at SlQt::WasmRestorePath() by the time it
	// does -- WebAssembly has no synchronous file picker to call from there. So the picking
	// happens here instead, first and asynchronously, before Starlane::RestoreGame() ever runs.
	QFileDialog::getOpenFileContent(tr("Starlane Save File (*.sls)"),
		[this](const QString &fileName, const QByteArray &fileContent) {
			if (fileName.isEmpty()) return;  // dialog was cancelled

			QFile staged(SlQt::WasmRestorePath());
			if (!staged.open(QIODevice::WriteOnly)) return;
			staged.write(fileContent);
			staged.close();

			BeginOutputBatch();
			Starlane::RestoreGame();
			EndOutputBatch();
			UpdateActionState();
			UpdateStatusBar();
		});
#else
	BeginOutputBatch();
	Starlane::RestoreGame();
	EndOutputBatch();
	UpdateActionState();
	UpdateStatusBar();
#endif
}

void MainWindow::ToggleTranscript() {
	if (transcribing) {
		StopTranscript();
		return;
	}

#ifdef __EMSCRIPTEN__
	// No dialog: WebAssembly has no synchronous "pick a save location". Write to a fixed scratch
	// path instead, and offer the accumulated content as a real download once transcribing stops
	// (see StopTranscript()) -- there's no interactive choice to make until then anyway.
	const QString path = QDir::tempPath() + QStringLiteral("/starlane-transcript.txt");
#else
	const QString path = QFileDialog::getSaveFileName(this, tr("Start Transcript"), QString(),
		tr("Text Files (*.txt)"));
	if (path.isEmpty()) return;
#endif

	auto *file = new QFile(path, this);
	if (!file->open(QIODevice::WriteOnly | QIODevice::Text)) {
		QMessageBox::warning(this, QStringLiteral("Starlane"),
			tr("Could not open \"%1\" for writing.").arg(QDir::toNativeSeparators(path)));
		delete file;
		return;
	}

	transcriptFile = file;
	transcribing = true;
	transcriptAction->setText(tr("Stop &Transcript"));
	formatter->SetTranscriptSink([this](const QString &text) { WriteTranscript(text); });
}

void MainWindow::WriteTranscript(const QString &text) {
	if (!transcriptFile) return;
	transcriptFile->write(text.toUtf8());
}

void MainWindow::StopTranscript() {
	if (!transcriptFile) return;
	formatter->SetTranscriptSink(nullptr);
#ifdef __EMSCRIPTEN__
	const QString path = transcriptFile->fileName();
#endif
	transcriptFile->close();
	delete transcriptFile;
	transcriptFile = nullptr;
	transcribing = false;
	transcriptAction->setText(tr("Start &Transcript"));

#ifdef __EMSCRIPTEN__
	// Offer the finished transcript as a real download now -- there was no chance to ask the
	// player where to put it up front (see ToggleTranscript()). Covers both this being reached
	// via the explicit "Stop Transcript" action and implicitly (LoadGameData(), window close):
	// either way, the transcript would otherwise be silently discarded on WASM specifically,
	// unlike native where it was already safely on disk the whole time.
	QFile finished(path);
	if (finished.open(QIODevice::ReadOnly))
		QFileDialog::saveFileContent(finished.readAll(), QStringLiteral("transcript.txt"), this);
#endif
}

void MainWindow::ReplayCommandsTriggered() {
#ifdef __EMSCRIPTEN__
	// No dialog: same reason as OpenGameTriggered()/RestoreGameTriggered() -- getOpenFileContent()
	// hands back the picked file's content directly, asynchronously, rather than a path.
	QFileDialog::getOpenFileContent(tr("Command Files (*.txt)"),
		[this](const QString &fileName, const QByteArray &fileContent) {
			if (fileName.isEmpty()) return;  // dialog was cancelled
			QBuffer buffer;
			buffer.setData(fileContent);
			buffer.open(QIODevice::ReadOnly);
			RunReplay(buffer);
		});
#else
	const QString path = QFileDialog::getOpenFileName(this, tr("Replay Commands"), QString(),
		tr("Command Files (*.txt)"));
	if (path.isEmpty()) return;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QMessageBox::warning(this, QStringLiteral("Starlane"),
			tr("Could not open \"%1\".").arg(QDir::toNativeSeparators(path)));
		return;
	}
	RunReplay(file);
#endif
}

void MainWindow::RunReplay(QIODevice &source) {
	isReplaying = true;
	input->setEnabled(false);
	UpdateActionState();

	QTextStream in(&source);
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

void MainWindow::OnWaitKey(OutputFormatter *fmt) {
	input->setEnabled(false);
	waitingFormatter = fmt;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
	if (waitingFormatter && (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress)) {
		OutputFormatter *fmt = waitingFormatter;
		waitingFormatter = nullptr;
		input->setEnabled(true);
		input->setFocus();
		fmt->ResumeAfterWaitKey();  // may hit another <waitkey> and call OnWaitKey() again, immediately
		return true;  // consume: don't let this key/click also reach whatever widget it landed on
	}
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
	wasClosed = true;
	// Nothing to resume -- OutputFormatter::ResumeAfterWaitKey() would just render more text into
	// a window that's going away.
	waitingFormatter = nullptr;
	StopTranscript();
	QSettings().setValue(kGeometrySettingsKey, saveGeometry());
	QMainWindow::closeEvent(event);
	qApp->quit();
}
