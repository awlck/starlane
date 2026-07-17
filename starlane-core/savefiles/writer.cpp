//
// Created by Adrian Welcker on 06.06.23.
//

#include "writer.h"

#include <miniz.h>

#include "../game.h"

namespace Starlane::Save {

static constexpr size_t WRITER_BUFSIZE = 1024;

Writer::Writer(void *target, const Starlane::Game *game)
		: hFile(target), textbuf(new uint8_t[WRITER_BUFSIZE]), zbuf(new uint8_t[WRITER_BUFSIZE])
{
	stream = new mz_stream();
	stream->avail_in = 0;
	stream->next_in = textbuf;
	stream->next_out = zbuf;
	stream->avail_out = WRITER_BUFSIZE;
	mz_deflateInit(stream, MZ_DEFAULT_COMPRESSION);
	BeginNamedCompound("meta");
	WriteKV("type", "starlane_save");
	WriteKV("version", currentSaveFileVer);
	WriteKV("game_title", game->GetTitle());
	WriteKV("game_author", game->GetAuthor());
	WriteKV("game_revision", game->GetLastUpdated());
	WriteKV("game_checksum", game->GetChecksum());
	EndCompound();
}

Writer::~Writer() {
	RunCompressor(true);
	mz_deflateEnd(stream);
	delete[] textbuf;
	delete[] zbuf;
	delete stream;
	frontend->CloseFile(hFile);
}

void Writer::AcceptChar(char c) {
	textbuf[position++] = c;
	if (position < WRITER_BUFSIZE) return;

	// Buffer is full, run compressor and write out as needed
	RunCompressor(false);
}

void Writer::WriteUnqouted(const char *str) {
	for (const char *p = str; *p; ++p) {
		AcceptChar(*p);
		if (*p == '\n') {
			for (size_t i = 0; i < indentLevel; i++)
				AcceptChar('\t');
		}
	}
}

void Writer::WriteLiteralString(const char *str) {
	// First pass: determine whether we need to escape this string:
	bool needQuotes = false;
	size_t cnt = 0;
	for (const char *p = str; *p; ++p, ++cnt) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') || cnt >= 64) {
			needQuotes = true;
			break;
		}
	}
	if (cnt == 0) needQuotes = true;
	// Now write out the string, adding quotes and escape sequences if necessary
	if (needQuotes) {
		AcceptChar('"');
		for (const char *p = str; *p; ++p) {
			switch (*p) {
			case '\n':
				AcceptChar('\\');
				AcceptChar('n');
				break;
			case '\t':
				AcceptChar('\\');
				AcceptChar('t');
				break;
			case '"':
			case '\\':
				AcceptChar('\\');
				[[fallthrough]];
			default:
				AcceptChar(*p);
			}
		}
		AcceptChar('"');
	} else {
		for (const char *p = str; *p; ++p)
			AcceptChar(*p);
	}
}

void Writer::WriteLiteralString(const std::string_view &sv) {
	// basically the same as the above but taking advantage of the fact that we know the length beforehand
	bool needQuotes = (sv.size() >= 64 || sv.empty());
	if (!needQuotes) {
		for (char p : sv) {
			if (!((p >= 'a' && p <= 'z') || (p >= 'A' && p <= 'Z') || (p >= '0' && p <= '9') || p == '_')) {
				needQuotes = true;
				break;
			}
		}
	}
	if (needQuotes) {
		AcceptChar('"');
		for (char p: sv) {
			switch (p) {
			case '\n':
				AcceptChar('\\');
				AcceptChar('n');
				break;
			case '\t':
				AcceptChar('\\');
				AcceptChar('t');
				break;
			case '"':
			case '\\':
				AcceptChar('\\');
				[[fallthrough]];
			default:
				AcceptChar(p);
			}
		}
		AcceptChar('"');
	} else {
		for (char p: sv)
			AcceptChar(p);
	}
}

void Writer::RunCompressor(bool finish) {
	stream->next_in = textbuf;
	stream->avail_in = position;
	do {
		stream->avail_out = WRITER_BUFSIZE;
		stream->next_out = zbuf;
		[[maybe_unused]] int status = mz_deflate(stream, finish ? MZ_FINISH : MZ_NO_FLUSH);
		// TODO: error handling
		mz_ulong toWrite = WRITER_BUFSIZE - stream->avail_out;
		frontend->WriteFile(hFile, zbuf, toWrite);
	} while (stream->avail_in > 0 || stream->avail_out < WRITER_BUFSIZE);
	position = 0;
}

}