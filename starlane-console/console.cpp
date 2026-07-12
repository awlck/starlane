// A minimal console frontend for starlane-core, intended for testing.
// Prints all output text verbatim (no HTML interpretation) and reads
// player input from stdin.

#include <starlane-core.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace SlConsole {

void FatalError(const char *msg) {
	std::fprintf(stderr, "Fatal error: %s\n", msg);
}

void OutputText(const char *msg) {
	std::fputs(msg, stdout);
}

std::string StrToUpperCase(const std::string &s) {
	std::string result = s;
	for (char &c : result) c = (char) std::toupper((unsigned char) c);
	return result;
}

std::string StrToLowerCase(const std::string &s) {
	std::string result = s;
	for (char &c : result) c = (char) std::tolower((unsigned char) c);
	return result;
}

std::string StrToSentenceCase(const std::string &s) {
	std::string result = StrToLowerCase(s);
	for (char &c : result) {
		if (!std::isspace((unsigned char) c)) {
			c = (char) std::toupper((unsigned char) c);
			break;
		}
	}
	return result;
}

void *CreateSaveFile() {
	std::cout << "Save file name: " << std::flush;
	std::string path;
	if (!std::getline(std::cin, path) || path.empty()) return nullptr;
	return std::fopen(path.c_str(), "wb");
}

void *OpenSaveFile() {
	std::cout << "Restore from file: " << std::flush;
	std::string path;
	if (!std::getline(std::cin, path) || path.empty()) return nullptr;
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
	bool quitAfterLoad = false;

	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--quit") {
			quitAfterLoad = true;
		} else if (tafPath.empty()) {
			tafPath = arg;
		}
	}

	if (tafPath.empty()) {
		std::fprintf(stderr, "Usage: %s [--quit] <game.taf>\n", argv[0]);
		return 1;
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
	Starlane::CreateGame(data, (size_t) fsize);
	delete[] data;
	Starlane::BeginGame();

	if (quitAfterLoad) return 0;

	std::string line;
	while (Starlane::GameIsOngoing()) {
		std::cout << "> " << std::flush;
		if (!std::getline(std::cin, line)) break;
		Starlane::ProcessInput(line);
	}

	return 0;
}
