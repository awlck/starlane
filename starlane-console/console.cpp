// A minimal console frontend for starlane-core, intended for testing.
// Prints all output text verbatim (no HTML interpretation) and reads
// player input from stdin.

#include <starlane-core.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <clocale>
#include <cwchar>
#include <cwctype>
#include <vector>
#endif

namespace SlConsole {

// Where to read player input (and prompt responses) from: stdin by default,
// or a file given via --input, so input doesn't have to be piped through the
// shell (which gets in the way when running under a debugger).
std::istream *gInput = &std::cin;

void FatalError(const char *msg) {
	std::fprintf(stderr, "Fatal error: %s\n", msg);
}

void OutputText(const char *msg) {
	std::fputs(msg, stdout);
}

// Player input and game text are UTF-8, so case conversion needs to work on whole codepoints
// rather than individual bytes -- plain byte-wise std::toupper/tolower (as used here previously)
// silently leaves every non-ASCII letter untouched. This frontend is used both as the reference
// for regression transcripts and to expose these functions to game authors via expressions, so
// getting non-ASCII text right actually matters here, unlike a purely cosmetic display glitch.
//
// starlane-core deliberately isn't linked against a Unicode library (e.g. ICU) itself -- Qt and
// Glk already ship their own case-folding facilities, and pulling in ICU directly would be an
// extra dependency to vendor/find on every platform (notably complicating a future WASM build of
// the Qt frontend). This frontend has neither Qt nor a real Glk library available, so it instead
// leans on Unicode-aware facilities the platform C/OS library already provides.

#if defined(_WIN32)

// On Windows, wchar_t is UTF-16, and LCMapStringEx gives full Unicode default case mapping (which
// can change a string's length, e.g. German "ß" upper-casing to "SS") without depending on the
// user's locale settings -- LOCALE_NAME_INVARIANT asks for culture-invariant mapping, so results
// stay deterministic (e.g. no Turkish dotless-i behavior) regardless of the host's configuration.

namespace {

std::wstring Utf8ToWide(const std::string &s) {
	if (s.empty()) return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
	std::wstring w((size_t) n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
	return w;
}

std::string WideToUtf8(const std::wstring &w) {
	if (w.empty()) return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t) n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), s.data(), n, nullptr, nullptr);
	return s;
}

std::wstring MapCase(const std::wstring &w, DWORD flag) {
	if (w.empty()) return {};
	int n = LCMapStringEx(LOCALE_NAME_INVARIANT, flag, w.data(), (int) w.size(),
	                       nullptr, 0, nullptr, nullptr, 0);
	if (n <= 0) return w;  // mapping failed: leave the text as-is rather than losing it
	std::wstring out((size_t) n, L'\0');
	n = LCMapStringEx(LOCALE_NAME_INVARIANT, flag, w.data(), (int) w.size(),
	                   out.data(), n, nullptr, nullptr, 0);
	if (n <= 0) return w;
	out.resize((size_t) n);
	return out;
}

bool IsAsciiSpace(wchar_t c) {
	return c < 128 && std::isspace((int) c);
}

}  // namespace

std::string StrToUpperCase(const std::string &s) {
	return WideToUtf8(MapCase(Utf8ToWide(s), LCMAP_UPPERCASE));
}

std::string StrToLowerCase(const std::string &s) {
	return WideToUtf8(MapCase(Utf8ToWide(s), LCMAP_LOWERCASE));
}

std::string StrToSentenceCase(const std::string &s) {
	std::wstring w = MapCase(Utf8ToWide(s), LCMAP_LOWERCASE);
	for (size_t i = 0; i < w.size(); i++) {
		if (IsAsciiSpace(w[i])) continue;
		// Keep a surrogate pair together so a non-BMP character (e.g. an emoji) doesn't get torn
		// in half by mapping only its first UTF-16 code unit.
		size_t len = (w[i] >= 0xD800 && w[i] <= 0xDBFF && i + 1 < w.size() &&
		              w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) ? 2 : 1;
		w.replace(i, len, MapCase(w.substr(i, len), LCMAP_UPPERCASE));
		break;
	}
	return WideToUtf8(w);
}

#else  // macOS/Linux: wchar_t is a 32-bit Unicode codepoint there, so no surrogate pairs to worry
       // about, and the platform libc's towupper/towlower/iswspace already understand all of
       // Unicode once put in a UTF-8 locale.

namespace {

// mbsrtowcs/wcsrtombs need LC_CTYPE to name a UTF-8 locale to treat our strings as UTF-8 at all --
// the "C" locale this process starts in only understands single-byte ASCII. Pick an explicit
// locale rather than trusting the environment (LC_CTYPE may well be unset on a CI runner or under
// cron), so this frontend's output stays deterministic regardless of who's running it.
void EnsureUtf8Locale() {
	static const bool initialized = [] {
		for (const char *name : { "en_US.UTF-8", "C.UTF-8" }) {
			if (std::setlocale(LC_CTYPE, name)) return true;
		}
		return false;
	}();
	(void) initialized;
}

std::wstring Utf8ToWide(const std::string &s) {
	EnsureUtf8Locale();
	std::vector<wchar_t> buf(s.size() + 1);  // upper bound: at most 1 codepoint per source byte
	std::mbstate_t state{};
	const char *src = s.c_str();
	size_t n = std::mbsrtowcs(buf.data(), &src, buf.size(), &state);
	if (n == (size_t) -1) return {};  // invalid UTF-8 (shouldn't happen for our own game data)
	return std::wstring(buf.data(), n);
}

std::string WideToUtf8(const std::wstring &w) {
	EnsureUtf8Locale();
	std::vector<char> buf(w.size() * 4 + 1);  // upper bound: 4 UTF-8 bytes per codepoint
	std::mbstate_t state{};
	const wchar_t *src = w.c_str();
	size_t n = std::wcsrtombs(buf.data(), &src, buf.size(), &state);
	if (n == (size_t) -1) return {};
	return std::string(buf.data(), n);
}

bool IsAsciiSpace(wchar_t c) {
	return c < 128 && std::isspace((int) c);
}

}  // namespace

std::string StrToUpperCase(const std::string &s) {
	std::wstring w = Utf8ToWide(s);
	for (wchar_t &c : w) c = (wchar_t) std::towupper((wint_t) c);
	return WideToUtf8(w);
}

std::string StrToLowerCase(const std::string &s) {
	std::wstring w = Utf8ToWide(s);
	for (wchar_t &c : w) c = (wchar_t) std::towlower((wint_t) c);
	return WideToUtf8(w);
}

std::string StrToSentenceCase(const std::string &s) {
	std::wstring w = Utf8ToWide(s);
	for (wchar_t &c : w) c = (wchar_t) std::towlower((wint_t) c);
	for (wchar_t &c : w) {
		if (IsAsciiSpace(c)) continue;
		c = (wchar_t) std::towupper((wint_t) c);
		break;
	}
	return WideToUtf8(w);
}

#endif

// Read one line, dropping a trailing CR so that command files saved on Windows work as-is.
std::istream &ReadLine(std::istream &in, std::string &line) {
	if (std::getline(in, line) && !line.empty() && line.back() == '\r')
		line.pop_back();
	return in;
}

bool AskYesNo(const char *question) {
	std::string answer;
	for (;;) {
		std::cout << question << " (y/n) " << std::flush;
		if (!ReadLine(*gInput, answer)) return false;  // no more input: assume "no"
		answer = StrToLowerCase(answer);
		if (answer == "y" || answer == "yes") return true;
		if (answer == "n" || answer == "no") return false;
	}
}

void QuitGame() {
	// Nothing to do: the main loop notices that the game is over and stops asking for input.
}

void PumpEvents() {
	// Nothing to do: this frontend has no event loop of its own to pump.
}

void *CreateSaveFile() {
	std::cout << "Save file name: " << std::flush;
	std::string path;
	if (!ReadLine(*gInput, path) || path.empty()) return nullptr;
	return std::fopen(path.c_str(), "wb");
}

void *OpenSaveFile() {
	std::cout << "Restore from file: " << std::flush;
	std::string path;
	if (!ReadLine(*gInput, path) || path.empty()) return nullptr;
	return std::fopen(path.c_str(), "rb");
}

size_t ReadFile(void *handle, uint8_t *buffer, size_t bufsize) {
	return std::fread(buffer, 1, bufsize, (FILE *) handle);
}

void WriteFile(void *handle, const uint8_t *buffer, size_t count) {
	std::fwrite(buffer, 1, count, (FILE *) handle);
}

void CloseFile(void *handle) {
	std::fclose((FILE *) handle);
}

}  // namespace SlConsole

int main(int argc, char **argv) {
	using namespace SlConsole;

	std::string tafPath;
	std::string inputPath;
	bool quitAfterLoad = false;
	uint32_t randomSeed = 0;  // 0 means "seed from the OS", see Starlane::SeedRNG

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--quit") {
			quitAfterLoad = true;
		} else if (arg == "--input") {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "--input requires a file path\n");
				return 1;
			}
			inputPath = argv[++i];
		} else if (arg == "--seed") {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "--seed requires a number\n");
				return 1;
			}
			randomSeed = (uint32_t) std::strtoul(argv[++i], nullptr, 10);
		} else if (tafPath.empty()) {
			tafPath = arg;
		}
	}

	if (tafPath.empty()) {
		std::fprintf(stderr, "Usage: %s [--quit] [--input <commands.txt>] [--seed <n>] <game.taf>\n", argv[0]);
		return 1;
	}

	std::ifstream inputFile;
	if (!inputPath.empty()) {
		inputFile.open(inputPath);
		if (!inputFile) {
			std::fprintf(stderr, "Could not open '%s'\n", inputPath.c_str());
			return 1;
		}
		gInput = &inputFile;
	}

	FILE *f = std::fopen(tafPath.c_str(), "rb");
	if (!f) {
		std::fprintf(stderr, "Could not open '%s'\n", tafPath.c_str());
		return 1;
	}
	std::fseek(f, 0, SEEK_END);
	long fsize = std::ftell(f);
	std::rewind(f);
	auto *data = new uint8_t[fsize];
	std::fread(data, 1, fsize, f);
	std::fclose(f);

	Starlane::Frontend fe {
		/* .randomSeed = */ randomSeed,
		/* .timersAvailable = */ false,
		/* .FatalError = */ &FatalError,
		/* .OutputText = */ &OutputText,
		/* .StrToUpperCase = */ &StrToUpperCase,
		/* .StrToLowerCase = */ &StrToLowerCase,
		/* .StrToSentenceCase = */ &StrToSentenceCase,
		/* .AskYesNo = */ &AskYesNo,
		/* .QuitGame = */ &QuitGame,
		/* .PumpEvents = */ &PumpEvents,
		/* .CreateSaveFile = */ &CreateSaveFile,
		/* .OpenSaveFile = */ &OpenSaveFile,
		/* .ReadFile = */ &ReadFile,
		/* .WriteFile = */ &WriteFile,
		/* .CloseFile = */ &CloseFile
	};
	Starlane::InitBackend(&fe);
	Starlane::CreateGame(data, (size_t) fsize);
	delete[] data;
	Starlane::BeginGame();

	if (quitAfterLoad) return 0;

	std::string line;
	while (Starlane::GameIsOngoing()) {
		std::cout << "\n> " << std::flush;
		if (!ReadLine(*gInput, line)) break;
		// When input is scripted, echo it: the transcript is otherwise unreadable, since
		// nothing else records which command produced which output.
		if (gInput != &std::cin) std::cout << line << std::endl;
		Starlane::ProcessInput(line);
	}

	return 0;
}
