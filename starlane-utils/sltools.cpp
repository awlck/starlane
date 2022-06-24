#include <iostream>
#include <string.h>

#include <taffile.h>

void mapper_run(const std::string &gametxt);
void textdump_run(const std::string &gametxt);

namespace SLFrontend {
void FatalError(const char *msg) {
	fprintf(stderr, "%s\n", msg);
}
}

int main(int argc, char **argv) {
	if (argc != 3) {
		std::cerr << "USAGE:  sltools [tool] [file]" << std::endl;
		return 1;
	}

	auto f = fopen(argv[2], "rb");
	fseek(f, 0, SEEK_END);
	size_t fsize = ftell(f);
	rewind(f);
	auto input = new uint8_t[fsize];
	fread(input, fsize, 1, f);
	fclose(f);
	auto result = Starlane::ExtractTaf(input, fsize);

	if (strcmp(argv[1], "txtdump") == 0) {
		textdump_run(result);
	} else if (strcmp(argv[1], "mapper") == 0) {
		mapper_run(result);
	}

	return 0;
}
