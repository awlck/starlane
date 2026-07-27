//
// Created by Adrian Welcker on 06.06.23.
//

#include "writer.h"

#include <cstring>

#include <miniz.h>

#include "../error.h"
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
	// A throwing destructor is a hard std::terminate() away from happening (this runs during
	// unwinding as often as not), so a compression failure on this final flush can only be logged,
	// not propagated -- RunCompressor's other caller (AcceptChar, mid-write) is free to throw.
	try {
		RunCompressor(true);
	} catch (const Exception &e) {
		LogError(e.what());
	}
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

// Whether this text would come back as something other than a string if it were written without
// quotes. The lexer reads a bare word starting with a digit or '-' as an integer, and "yes"/"no"
// as a boolean (savefiles/parser.cpp's Lex), so a text value that happens to look like one of
// those changes type on the way back in unless it is quoted. Skybreak has text variables holding
// "0", which is how this was found: restoring one rejected the whole save file.
static bool WouldNotReadBackAsText(std::string_view sv) {
	if (sv.empty()) return false;  // the empty string is quoted anyway
	if ((sv[0] >= '0' && sv[0] <= '9') || sv[0] == '-') return true;
	return sv == "yes" || sv == "YES" || sv == "no" || sv == "NO";
}

void Writer::WriteLiteralString(const char *str) {
	// First pass: determine whether we need to escape this string:
	bool needQuotes = WouldNotReadBackAsText(str);
	size_t cnt = 0;
	if (!needQuotes) {
		for (const char *p = str; *p; ++p, ++cnt) {
			if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') || cnt >= 64) {
				needQuotes = true;
				break;
			}
		}
	} else {
		cnt = strlen(str);
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
	bool needQuotes = (sv.size() >= 64 || sv.empty() || WouldNotReadBackAsText(sv));
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
	for (;;) {
		stream->avail_out = WRITER_BUFSIZE;
		stream->next_out = zbuf;
		int status = mz_deflate(stream, finish ? MZ_FINISH : MZ_NO_FLUSH);
		// We only ever feed the compressor buffers it was itself initialized with, so the only way
		// to land here is a real internal fault (e.g. out of memory) -- surface it rather than
		// silently writing a truncated/corrupt save file.
		if (status < 0)
			throw Exception(std::string("Save file compression failed: ") + (mz_error(status) ? mz_error(status) : "unknown error"));
		mz_ulong toWrite = WRITER_BUFSIZE - stream->avail_out;
		frontend->WriteFile(hFile, zbuf, toWrite);
		// Mid-stream, our job is done as soon as the compressor has taken all the input: whatever
		// it is still holding back comes out on a later call. Asking it for more with nothing left
		// to give it is what MZ_BUF_ERROR means, and the previous loop condition ("go round again
		// if the last call produced anything at all") did exactly that whenever a deflate happened
		// to fill the output buffer to the byte -- which is why saving a game with a lot of state
		// (Lost Coastlines, Skybreak) failed partway through and left a truncated file behind.
		if (finish ? status == MZ_STREAM_END : stream->avail_in == 0)
			break;
	}
	position = 0;
}

}