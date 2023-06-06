#include <QtCore/QString>
#include <QtWidgets/QApplication>

#include <starlane-core.h>
#include <clocale>

#include "mainwindow.h"

MainWindow *theWin = nullptr;

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


int main(int argc, char **argv) {
	QApplication app(argc, argv);
	Starlane::Frontend fe {
		.randomSeed = 0,
		.FatalError = &FatalError,
		.OutputText = &OutputText,
		.StrToUpperCase = &StrToUpperCase,
		.StrToLowerCase = &StrToLowerCase,
		.StrToSentenceCase = &StrToSentenceCase
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
	return app.exec();
}
