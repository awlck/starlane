#include <string>
#include <stdio.h>

#include <starlane-core.h>
#include <taffile.h>
#include <game.h>
#include <gamecontent/restriction.h>
#include <expression.h>

#include <iostream>
#include <locale>

#define PSAPI_VERSION 2
#include <Windows.h>
#include <Psapi.h>
#include <processthreadsapi.h>

namespace {
wchar_t *TextTransmute(const char *s) {
	int numChars = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t *buf = new wchar_t[numChars];
	MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, numChars);
	return buf;
}
std::string TextUntransmute(wchar_t *s) {
	int numBytes = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
	char *buf = new char[numBytes];
	WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, numBytes, NULL, NULL);
	std::string result(buf);
	delete[] s;
	delete[] buf;
	return result;
}
}

namespace SLFrontend {
void FatalError(const char *msg) {
	fprintf(stderr, "%s\n", msg);
}
void OutputText(const char *msg) {
	puts(msg);
}

// Windows implementation of case-changing stuff
namespace Services {
std::string StrToUpperCase(const std::string &str) {
	if (str.empty()) return "";
	auto *txtIn = TextTransmute(str.c_str());
	// o boi do i ever love using win32 api stuff
	auto charsInResult = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_FULLWIDTH | LCMAP_LINGUISTIC_CASING | LCMAP_UPPERCASE, txtIn, -1, NULL, 0, NULL, NULL, 0);
	auto *txtOut = new wchar_t[charsInResult];
	LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LINGUISTIC_CASING | LCMAP_UPPERCASE, txtIn, -1, txtOut, charsInResult, NULL, NULL, 0);
	delete[] txtIn;
	return TextUntransmute(txtOut);
}
std::string StrToLowerCase(const std::string &str) {
	if (str.empty()) return "";
	auto *txtIn = TextTransmute(str.c_str());
	auto charsInResult = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_FULLWIDTH | LCMAP_LINGUISTIC_CASING | LCMAP_LOWERCASE, txtIn, -1, NULL, 0, NULL, NULL, 0);
	auto *txtOut = new wchar_t[charsInResult];
	LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LINGUISTIC_CASING | LCMAP_LOWERCASE, txtIn, -1, txtOut, charsInResult, NULL, NULL, 0);
	delete[] txtIn;
	return TextUntransmute(txtOut);
}
std::string StrToSentenceCase(const std::string &str) {
	if (str.empty()) return "";
	int numChars = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
	wchar_t *txtIn = new wchar_t[numChars];
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, txtIn, numChars);
	// title-casing is obviously not the same as sentence-casing (for English, anyways), but we use it as an approximation for the buffer size we'll need.
	auto tentativeResultSize = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LINGUISTIC_CASING | LCMAP_TITLECASE, txtIn, -1, NULL, 0, NULL, NULL, 0) + 32;  // meh
	auto *buf = new wchar_t[tentativeResultSize];
	// process first character
	int charsThisRound = 1;
	if (txtIn[0] > (wchar_t) 0b1101110000000000) {  // surrogate pair
		charsThisRound = 2;
	}
	int charsProcessed = 0;
	auto charsOutput = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LINGUISTIC_CASING | LCMAP_UPPERCASE, txtIn, charsThisRound, buf + charsProcessed, tentativeResultSize - charsProcessed, NULL, NULL, NULL);
	charsProcessed += charsOutput;
	charsThisRound = 0;
	bool beginningOfSentence = false;
	for (size_t pos = 1; pos < numChars - 1; ++pos) {
		if (isspace(txtIn[pos])) continue;
		if (txtIn[pos] == L'.' || txtIn[pos] == L'!' || txtIn[pos] == '?') {
			beginningOfSentence = true;
			continue;
		}
		if (beginningOfSentence) { // lowercase everything up to here, then uppercase next letter
			charsThisRound = pos - charsProcessed;
			charsOutput = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LINGUISTIC_CASING | LCMAP_LOWERCASE, txtIn, charsThisRound, buf + charsProcessed, tentativeResultSize - charsProcessed, NULL, NULL, NULL);
			if (charsOutput == 0) {
				throw std::runtime_error("Can't handle sentence-casing. Go increase the buffer size or smth.");
			}
			charsProcessed += charsOutput;
			charsThisRound = 1;
			if (txtIn[0] > (wchar_t) 0b1101110000000000) {  // surrogate pair
				charsThisRound = 2;
				pos += 1;
			}
			charsOutput = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LINGUISTIC_CASING | LCMAP_UPPERCASE, txtIn, charsThisRound, buf + charsProcessed, tentativeResultSize - charsProcessed, NULL, NULL, NULL);
			charsProcessed += charsOutput;
			charsThisRound = 0;
		}
	}
	return TextUntransmute(buf);
}
}

}

int main(int argc, char **argv) {
	::setlocale(LC_ALL, ".utf-8");
	std::locale::global(std::locale(".utf-8"));
	auto f = fopen(R"(C:\Users\Adrian\OneDrive\Temp\ADRIFT5\lost-coastlines\lost-coastlines-v1.2.taf)", "rb");
	fseek(f, 0, SEEK_END);
	size_t fsize = ftell(f);
	rewind(f);
	uint8_t *input = new uint8_t[fsize];
	fread(input, fsize, 1, f);
	fclose(f);
	auto result = Starlane::ExtractTaf(input, fsize);
	auto game = Starlane::Game::LoadFromXML(result);
	game->Begin();

	HANDLE myself = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS pmc;
	GetProcessMemoryInfo(myself, &pmc, sizeof(pmc));
	std::cerr << "Before saves: " << pmc.WorkingSetSize / 1024 << " KB" << std::endl;
	for (int i = 0; i < 256; i++) {
		game->SaveUndo();
		GetProcessMemoryInfo(myself, &pmc, sizeof(pmc));
		std::cerr << i+1 << " saves: " << pmc.WorkingSetSize / 1024 << " KB" << std::endl;
	}

	return 0;
}