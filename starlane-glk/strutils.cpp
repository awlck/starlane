//
// Created by Adrian Welcker on 17.07.26.
//

#include "starlane-glk-internal.h"

#include <cctype>

// Decodes a UTF-8 string into Unicode codepoints, as needed for the Glk `_uni` calls. Invalid
// byte sequences are skipped rather than rejected -- the input always comes from either our own
// game data (already-validated UTF-8) or player-typed text, neither of which is worth failing
// on.
std::vector<uint32_t> Utf8ToUtf32(const std::string &s) {
	std::vector<uint32_t> out;
	size_t i = 0, n = s.size();
	while (i < n) {
		auto c = (unsigned char) s[i];
		uint32_t cp;
		int len;
		if (c < 0x80) { cp = c; len = 1; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
		else { i++; continue; }
		if (i + (size_t) len > n) break;
		bool valid = true;
		for (int k = 1; k < len; k++) {
			auto cc = (unsigned char) s[i + k];
			if ((cc & 0xC0) != 0x80) { valid = false; break; }
			cp = (cp << 6) | (cc & 0x3F);
		}
		if (!valid) { i++; continue; }
		out.push_back(cp);
		i += len;
	}
	return out;
}

std::string Utf32ToUtf8(const uint32_t *buf, size_t count) {
	std::string out;
	for (size_t i = 0; i < count; i++) {
		uint32_t cp = buf[i];
		if (cp < 0x80) {
			out += (char) cp;
		} else if (cp < 0x800) {
			out += (char) (0xC0 | (cp >> 6));
			out += (char) (0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char) (0xE0 | (cp >> 12));
			out += (char) (0x80 | ((cp >> 6) & 0x3F));
			out += (char) (0x80 | (cp & 0x3F));
		} else {
			out += (char) (0xF0 | (cp >> 18));
			out += (char) (0x80 | ((cp >> 12) & 0x3F));
			out += (char) (0x80 | ((cp >> 6) & 0x3F));
			out += (char) (0x80 | (cp & 0x3F));
		}
	}
	return out;
}

std::string Utf32ToUtf8(const std::vector<uint32_t> &buf) {
	return Utf32ToUtf8(buf.data(), buf.size());
}

void Utf8PopBack(std::string &s) {
	if (s.empty()) return;
	size_t i = s.size() - 1;
	while (i > 0 && (((unsigned char) s[i]) & 0xC0) == 0x80) i--;
	s.erase(i);
}

namespace {

// Runs one of the Glk buffer-case-conversion functions over `src`, retrying with a larger buffer
// if the conversion needed more room than our initial guess (Unicode case changes can change a
// string's length, e.g. German "ß" upper-casing to "SS").
std::vector<uint32_t> ConvertCase(const std::vector<uint32_t> &src, bool toUpper) {
	if (src.empty()) return {};
	size_t cap = src.size() * 3 + 4;
	std::vector<uint32_t> buf(src.begin(), src.end());
	buf.resize(cap);
	auto convert = toUpper ? &glk_buffer_to_upper_case_uni : &glk_buffer_to_lower_case_uni;
	glui32 n = convert(buf.data(), (glui32) cap, (glui32) src.size());
	if (n > cap) {
		buf.assign(src.begin(), src.end());
		buf.resize(n);
		n = convert(buf.data(), n, (glui32) src.size());
	}
	buf.resize(n);
	return buf;
}

bool IsAsciiSpace(uint32_t cp) {
	return cp < 128 && std::isspace((int) cp);
}

}  // namespace

std::string StrToUpperCase(const std::string &str) {
	auto codepoints = Utf8ToUtf32(str);
	return Utf32ToUtf8(ConvertCase(codepoints, /* toUpper = */ true));
}

std::string StrToLowerCase(const std::string &str) {
	auto codepoints = Utf8ToUtf32(str);
	return Utf32ToUtf8(ConvertCase(codepoints, /* toUpper = */ false));
}

std::string StrToSentenceCase(const std::string &str) {
	auto codepoints = ConvertCase(Utf8ToUtf32(str), /* toUpper = */ false);
	for (size_t i = 0; i < codepoints.size(); i++) {
		if (IsAsciiSpace(codepoints[i])) continue;
		// A single-character buffer's title-case is exactly its upper-case, so there's no need
		// for the dedicated (and more awkward to call) glk_buffer_to_title_case_uni here.
		std::vector<uint32_t> one = { codepoints[i] };
		one = ConvertCase(one, /* toUpper = */ true);
		codepoints.erase(codepoints.begin() + (long) i);
		codepoints.insert(codepoints.begin() + (long) i, one.begin(), one.end());
		break;
	}
	return Utf32ToUtf8(codepoints);
}
