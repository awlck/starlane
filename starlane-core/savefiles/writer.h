//
// Created by Adrian Welcker on 06.06.23.
//
#pragma once

#ifndef SLC_SAVEFILES_WRITER_H
#define SLC_SAVEFILES_WRITER_H

#include <algorithm>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

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
// Bumped to -995 when a game checksum was added to the savefile.
// Bumped to -994 when events learned to run SetLook subevents: each event now carries a stack of
// look-description overrides that must survive a save/restore round trip.
// Bumped to -993 when a group with no properties of its own stopped being written out at all
// (ContinueRestore resets every group before applying the file's exceptions, same as
// descriptions_shown already did).
// Bumped to -992 when Events and Walks gained a saved `triggering_task` (ADRIFT's sTriggeringTask),
// the per-cycle memory that suppresses a child task's re-trigger of a control its parent handles.
constexpr int currentSaveFileVer = -992;

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
	// Write an unordered collection of strings as a list, in sorted order, for the same reason as
	// WriteSortedMap below: an unordered_set hands its contents out in an order that depends on how
	// the set was built, so writing it as-is would make a save file's bytes depend on the route the
	// game took to its state rather than on the state itself.
	template<typename Container> void WriteSortedKV(const char *key, const Container &values) {
		std::vector<const std::string *> items;
		items.reserve(values.size());
		for (const auto &v : values) items.push_back(&v);
		std::sort(items.begin(), items.end(),
		          [](const std::string *a, const std::string *b) { return *a < *b; });
		WriteKey(key);
		WriteUnqouted("{ ");
		for (const std::string *v : items) {
			WriteValue(*v);
			AcceptChar(' ');
		}
		AcceptChar('}');
	}
	// Write every entry of a string-keyed map, in key order. Property tables are unordered_maps
	// whose iteration order is an implementation detail -- and, now that PropHolder shares them
	// copy-on-write, not even stable between two games that reached the same state by different
	// routes. Sorting here keeps a save file's bytes a function of the game state alone.
	template<typename V> void WriteSortedMap(const std::unordered_map<std::string, V> &map) {
		std::vector<const std::pair<const std::string, V> *> entries;
		entries.reserve(map.size());
		for (const auto &kv : map) entries.push_back(&kv);
		std::sort(entries.begin(), entries.end(),
		          [](const auto *a, const auto *b) { return a->first < b->first; });
		for (const auto *kv : entries) WriteKV(kv->first.c_str(), kv->second);
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
