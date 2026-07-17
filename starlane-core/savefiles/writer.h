//
// Created by Adrian Welcker on 06.06.23.
//
#pragma once

#ifndef SLC_SAVEFILES_WRITER_H
#define SLC_SAVEFILES_WRITER_H

#include <type_traits>

#include "../slc_private.h"

struct mz_stream_s;

namespace Starlane::Save {

// Use negative version numbers while we're still undergoing major development.
// Bumped to -998 when events learned to run: save files gained a turn counter and a pile of
// per-event scheduling state, and Event's saved status names changed outright.
// Bumped to -997 when events learned to run seconds-measured subevents on a turn-based clock: each
// subevent gained a private countdown of its own that a save now has to carry.
// Bumped to -996 when characters learned to walk: each character now saves the scheduling state of
// every walk it has (status, countdown, sub-walk bookkeeping, and settled step/when rolls).
constexpr int currentSaveFileVer = -996;

namespace {
// Helper to determine whether there's a const_iterator for T.
// https://stackoverflow.com/a/7728728
template<typename T>
struct has_const_iterator
{
private:
	template<typename C> static char test(typename C::const_iterator*);
	template<typename C> static int  test(...);
public:
	enum { value = sizeof(test<T>(0)) == sizeof(char) };
};
}  // anonymous namespace

class Writer {
public:
	Writer(void *target, const Game *game);
	~Writer();
	void Indent() { indentLevel += 1; }
	void Dedent() { if (indentLevel > 0) indentLevel -= 1; }

	template<typename T> void WriteKV(const char *key, const T &val) {
		WriteKey(key);
		WriteValue(val);
	}
	// Write out a string value
	void WriteValue(const std::string &str) { WriteLiteralString(str); }
	void WriteValue(const char *str) { WriteLiteralString(str); }
	void WriteValue(const std::string_view &sv) { WriteLiteralString(sv); }
	// Write out a boolean
	void WriteValue(bool b) { WriteUnqouted(b ? "yes" : "no"); }
	// Write out an integer
	template<typename T> typename std::enable_if_t<std::is_integral_v<T>> WriteValue(T val) {
		std::string tmp(std::to_string(val));
		WriteUnqouted(tmp.c_str());
	}
	// Write out a container (e.g. vector<T>) where T is any of the above types
	template <typename Container>  // https://stackoverflow.com/a/7728728
	typename std::enable_if<has_const_iterator<Container>::value,
			void>::type WriteValue(const Container &lst) {
		WriteUnqouted("{ ");
		for (auto it = lst.cbegin(); it != lst.cend(); it++) {
			WriteValue(*it);
			AcceptChar(' ');
		}
		AcceptChar('}');
	}
	void BeginNamedCompound(const char *name, bool oneline = false) {
		WriteKey(name);
		AcceptChar('{');
		if (oneline) AcceptChar(' ');
		else Indent();
	}
	void EndCompound(bool oneline = false) {
		if (oneline) {
			WriteUnqouted(" }");
		} else{
			Dedent();
			WriteUnqouted("\n}");
		}
	}

	// Write out a string value, adding quotation marks and escaping special characters as needed.
	void WriteLiteralString(const char *str);
	void WriteLiteralString(const std::string_view &str);
	// Write in normal mode, adding indents as needed.
	void WriteUnqouted(const char *str);

private:
	void *hFile;
	size_t indentLevel = 0;

	void WriteKey(const char *key) {
		WriteUnqouted("\n");
		WriteUnqouted(key);
		WriteUnqouted(" = ");
	}

	// Actual writing function: accept a single character into the buffer, calling the compressor and
	// writing to the file as needed.
	void AcceptChar(char c);
	void RunCompressor(bool finish);

	uint8_t *textbuf, *zbuf;
	size_t position = 0;
	mz_stream_s *stream;
};

}

#endif  // !SLC_SAVEFILES_WRITER_H
