#include <starlane-core.h>
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv) {
	FILE *f = fopen(argv[1], "rb");
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	rewind(f);
	uint8_t *data = new uint8_t[sz];
	fread(data, 1, sz, f);
	fclose(f);
	std::string xml = Starlane::ExtractTaf(data, (size_t) sz);
	fwrite(xml.data(), 1, xml.size(), stdout);
	return 0;
}