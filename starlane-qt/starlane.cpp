#include <QtCore/QString>
#include <QtWidgets/QApplication>

#include <starlane-core.h>
#include <clocale>
#include <QtGui/QPalette>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyleFactory>

#include "mainwindow.h"

MainWindow *theWin = nullptr;

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
void ApplyDarkTheme() {
	qApp->setStyle(QStyleFactory::create("Fusion"));
	QPalette pal;
	pal.setColor(QPalette::Window, Qt::black);
	pal.setColor(QPalette::WindowText, Qt::white);
	pal.setColor(QPalette::Base, Qt::black);
	pal.setColor(QPalette::AlternateBase, QColor(30, 30, 30));
	pal.setColor(QPalette::ToolTipBase, Qt::white);
	pal.setColor(QPalette::ToolTipText, Qt::white);
	pal.setColor(QPalette::Text, Qt::white);
	pal.setColor(QPalette::Button, QColor(30, 30, 30));
	pal.setColor(QPalette::ButtonText, Qt::white);
	pal.setColor(QPalette::BrightText, Qt::red);
	pal.setColor(QPalette::Link, QColor(100, 180, 255));
	pal.setColor(QPalette::Highlight, QColor(45, 90, 140));
	pal.setColor(QPalette::HighlightedText, Qt::white);
	pal.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
	pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
	pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
	qApp->setPalette(pal);
}

}

#include "../starlane-core/game.h"

int main(int argc, char **argv) {
	using namespace SlQt;

	QApplication app(argc, argv);
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
		/* .CreateSaveFile = */ &CreateSaveFile,
		/* .OpenSaveFile = */ &OpenSaveFile,
		/* .ReadFile = */ &ReadFile,
		/* .WriteFile = */ &WriteFile,
		/* .CloseFile = */ &CloseFile
	};
	Starlane::InitBackend(&fe);
	theWin = new MainWindow;
	theWin->show();
	if (argc != 2) return 1;
	::setlocale(LC_ALL, ".utf-8");
	auto f = fopen(argv[1], "rb");
	fseek(f, 0, SEEK_END);
	size_t fsize = ftell(f);
	rewind(f);
	uint8_t *input = new uint8_t[fsize];
	fread(input, fsize, 1, f);
	fclose(f);
	QApplication::processEvents();
	Starlane::CreateGame(input, fsize);
	theWin->ApplyGameInfo();
	QApplication::processEvents();
	theWin->RunBeginGame();
	// Only now: there is nothing for a tick to advance until the game has begun.
	theWin->StartEventTimer();
	return app.exec();
}
