#include <QtCore/QString>
#include <QtWidgets/QApplication>

#include <starlane-core.h>
#include <clocale>
#include <QtCore/QFile>
#include <QtWidgets/QFileDialog>

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

std::string StrToSentenceCase(const std::string &s) {  // whatevs
	return QString::fromUtf8(s.c_str(), s.length()).toUpper().toUtf8().toStdString();
}

void *CreateSaveFile() {
	auto result = QFileDialog::getSaveFileName(theWin, "Select save file location", QString(), "Starlane Save File (*.sls)");
	if (result.isEmpty()) return nullptr;
	auto file = new QFile(result);
	file->open(QIODevice::WriteOnly);
	return file;
}

void *OpenSaveFile() {
	auto result = QFileDialog::getOpenFileName(theWin, "Select a save file", QString(), "Starlane Save File (*.sls)");
	if (result.isEmpty()) return nullptr;
	auto file = new QFile(result);
	file->open(QIODevice::ReadOnly);
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

}

#include "../starlane-core/game.h"

int main(int argc, char **argv) {
	using namespace SlQt;

	QApplication app(argc, argv);
	Starlane::Frontend fe {
		/* .randomSeed = */ 0,
		/* .timersAvailable = */ false,
		/* .FatalError = */ &FatalError,
		/* .OutputText = */ &OutputText,
		/* .StrToUpperCase = */ &StrToUpperCase,
		/* .StrToLowerCase = */ &StrToLowerCase,
		/* .StrToSentenceCase = */ &StrToSentenceCase,
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
	QApplication::processEvents();
	Starlane::BeginGame();
	Starlane::Game::Get()->Save();
	return app.exec();
}
