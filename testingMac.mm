#include <string>

#include <starlane-core.h>
#include <game.h>
#include <gamecontent/restriction.h>
#include <expression.h>

#include <iostream>

#import "Foundation/Foundation.h"

namespace SLFrontend {
void FatalError(const char *msg) {
	std::cerr << msg << std::endl;
}
void OutputText(const char *msg) {
	std::cout << msg << std::endl;
}

namespace Services {
std::string StrToLowerCase(const std::string &s) {
	auto macstr = [[NSString stringWithUTF8String: s.c_str()] localizedLowercaseString];
	auto tmp = [macstr UTF8String];
	std::string result(tmp);
	//[macstr dealloc];
	return result;
}
std::string StrToUpperCase(const std::string &s) {
	auto macstr = [[NSString stringWithUTF8String: s.c_str()] localizedUppercaseString];
	auto tmp = [macstr UTF8String];
	std::string result(tmp);
	//[macstr dealloc];
	return result;
}
std::string StrToSentenceCase(const std::string &s) {
	// this obviously does the wrong thing, but it'll do for testing purposes...
	auto macstr = [[NSString stringWithUTF8String: s.c_str()] localizedCapitalizedString];
	auto tmp = [macstr UTF8String];
	std::string result(tmp);
	//[macstr dealloc];
	return result;
}
}

}

int main(int argc, char **argv) {
	if (argc != 2) return 1;
	::setlocale(LC_ALL, ".utf-8");
	auto f = fopen(argv[1], "rb");
	fseek(f, 0, SEEK_END);
	size_t fsize = ftell(f);
	rewind(f);
	uint8_t *input = new uint8_t[fsize];
	fread(input, fsize, 1, f);
	fclose(f);
	Starlane::Frontend fe {.FatalError = &SLFrontend::FatalError, .OutputText = &SLFrontend::OutputText};
	Starlane::InitBackend(&fe);
	Starlane::CreateGame(input, fsize);
	Starlane::BeginGame();
	return 0;
}