//
// Created by Adrian Welcker on 17.07.26.
//

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static constexpr uint8_t V5ident[] = { 60, 66, 63, 201, 106, 135, 194, 207, 146, 69, 62, 97 };

#include "starlane-core.h"

// TODO: probably move these to separate files as it makes sense.
void FatalError(const char *msg);
void OutputText(const char *msg);
std::string StrToUpperCase(const std::string &str);
std::string StrToLowerCase(const std::string &str);
std::string StrToSentenceCase(const std::string &str);
bool AskYesNoQuestion(const char *question);
void *CreateSaveFile();
void *OpenSaveFile();
size_t ReadFile(void *ptr, uint8_t *buffer, size_t bufsize);
void WriteFile(void *ptr, const uint8_t *buffer, size_t count);
void CloseFile(void *ptr);

// Bit flags describing the (best-effort) text style in effect for a run of output text,
// as accumulated from nested ADRIFT-style markup tags. See AppendHtml() below.
enum TextStyleFlags : uint32_t {
	kStyleNormal   = 0,
	kStyleBold     = 1u << 0,
	kStyleItalic   = 1u << 1,
	kStyleCentered = 1u << 2,
	kStyleInput    = 1u << 3,  // `<c>...</c>`: text that reads like a command the player could type
	kStyleMonospace = 1u << 4,  // `<font face="...">` naming one of the known monospace fonts
};

// Parses a string that may contain ADRIFT's HTML-like output markup (`<b>`, `<i>`, `<c>`,
// `<center>`, `<font ...>`, `<br>`, `<cls>`, `<waitkey>`, `<del>`, ...; see clsUserSession.vb's
// bHasOutput for the canonical tag list) and writes styled text to the main window.
void AppendHtml(const std::string &html);
// Writes `text` to the main window using the closest available Glk style for `styleFlags`, and
// (via the garglk zcolor extension, where available) the given 24-bit RGB foreground color.
// 0xffffffff mirrors glkext.h's zcolor_Default, which isn't in scope yet at this point in the
// file -- glk.h (via glkext.h) must be included inside the extern "C" block below.
void OutputStyled(const std::string &text, uint32_t styleFlags, uint32_t color = 0xffffffff);
// Blocks until the player has entered a line of input on the main window, returning it as UTF-8.
std::string GetLineInput();
// Blocks until the player has pressed a key on the main window (for the `<waitkey>` tag).
void WaitForKeypress();
// Attempts to erase the last character of the most recently *flushed* output (for `<del>`, once
// there's nothing left in the current run to remove without having committed it to the window).
void UnputLastChar();

extern "C" {
#include "glkext.h"
#include "gi_blorb.h"
#include "glkstart.h"

static strid_t gamefile;
static unsigned int gamefile_start, gamefile_len;
static const char *init_err;

// The main (text buffer) output window and its stream, and the status bar above it.
// starlane-core does not yet expose a callback for status bar content (location/score), so for
// now the status window is created but left blank -- TODO once such a callback exists.
static winid_t gMainWin;
static winid_t gStatusWin;
static strid_t gMainStream;

// Whether images can be drawn in the main window, and whether sound is available at all --
// checked once at startup so <img>/<audio> handling can bail out early instead of going through
// a blorb lookup for nothing.
static bool gImagesSupported;
static bool gSoundSupported;
// ADRIFT supports 8 parallel audio channels, numbered 1-8; index 0 is unused.
static schanid_t gSoundChannels[9];
// The path most recently (successfully) started on each channel, so replaying the exact same
// sound that's merely paused resumes it instead of restarting it from the top.
static std::string gRecentlyPlayedSound[9];

// Holds a corrected copy of a Blorb game's bytes (see locate_gamefile() below). Kept alive for
// the whole program: the Glk library retains the memory stream opened on it as the backing store
// for the Blorb resource map, calling back into it whenever the game asks for an image or sound.
static std::vector<uint8_t> gBlorbData;

/* locate_gamefile:
   Given that gamefile contains a Glk stream, which may be a TAF
   file or a Blorb archive containing one, locate the beginning and
   end of the TAF data.
*/
int locate_gamefile(int isblorb)
{
	if (!isblorb) {
		/* The simple case. A bare TAF file was opened, so we don't use
		   Blorb at all. */
		gamefile_start = 0;
		glk_stream_set_position(gamefile, 0, seekmode_End);
		gamefile_len = glk_stream_get_position(gamefile);
		return TRUE;
	}
	else {
		/* A Blorb file. ADRIFT 5's own Blorb writer gets the FORM chunk's declared size
		   wrong -- it should be the file length minus the 8-byte "FORM"+size header,
		   stored big-endian per the IFF convention Blorb uses, but ADRIFT leaves it
		   incorrect -- which makes any spec-compliant Blorb reader, including our own
		   gi_blorb.c, refuse to load the file at all. FrankenDrift's GlkRunner works
		   around this by patching that one field in a copy of the file and handing the
		   fix to Glk via a temporary file stream (see MainSession.cs's constructor); we
		   do the same patch but via a Glk memory stream instead, since a modern Glk
		   library supports one and it avoids touching the filesystem. */
		glk_stream_set_position(gamefile, 0, seekmode_End);
		glui32 fileLen = glk_stream_get_position(gamefile);
		gBlorbData.resize(fileLen);
		glk_stream_set_position(gamefile, 0, seekmode_Start);
		glk_get_buffer_stream(gamefile, reinterpret_cast<char *>(gBlorbData.data()), fileLen);
		glk_stream_close(gamefile, nullptr);

		glui32 correctSize = fileLen - 8;
		gBlorbData[4] = (uint8_t) (correctSize >> 24);
		gBlorbData[5] = (uint8_t) (correctSize >> 16);
		gBlorbData[6] = (uint8_t) (correctSize >> 8);
		gBlorbData[7] = (uint8_t) correctSize;

		strid_t blorbStream = glk_stream_open_memory(
		  reinterpret_cast<char *>(gBlorbData.data()), fileLen, filemode_Read, 0);

		giblorb_err_t err;
		giblorb_result_t blorbres;
		giblorb_map_t *map;

		err = giblorb_set_resource_map(blorbStream);
		if (err) {
			init_err = "This Blorb file seems to be invalid.";
			return FALSE;
		}
		map = giblorb_get_resource_map();
		err = giblorb_load_resource(map, giblorb_method_FilePos,
		  &blorbres, giblorb_ID_Exec, 0);
		if (err) {
			init_err = "This Blorb file does not contain an executable Glulx chunk.";
			return FALSE;
		}
		gamefile = blorbStream;
		gamefile_start = blorbres.data.startpos;
		gamefile_len = blorbres.length;
		return TRUE;
	}
}

glkunix_argumentlist_t glkunix_arguments[] = {
	{ "", glkunix_arg_ValueFollows, "filename: The game file to load." },

{ NULL, glkunix_arg_End, NULL }
};

int glkunix_startup_code(glkunix_startup_t *data) {
	char *filename = NULL;
	int ix = 1;
	unsigned char buf[12];
	uint32_t res;
	if (filename) {
		init_err = "You must supply exactly one game file.";
		return TRUE;
	}
	filename = data->argv[ix];
	if (!filename) {
		init_err = "You must supply the name of a game file.";
		return TRUE;
	}

	glkunix_set_base_file(filename);
	gamefile = glkunix_stream_open_pathname(filename, FALSE, 1);
	if (!gamefile) {
		init_err = "The game file could not be opened.";
		return TRUE;
	}

	/* Now we have to check to see if it's a Blorb file. */

	glk_stream_set_position(gamefile, 0, seekmode_Start);
	res = glk_get_buffer_stream(gamefile, (char *)buf, 12);
	if (!res) {
		init_err = "The data in this stand-alone game is too short to read.";
		return TRUE;
	}

	if (memcmp(buf, V5ident, sizeof(V5ident)) == 0) {
		/* Load game directly from file. */
		locate_gamefile(FALSE);

		return TRUE;
	}
	else if (buf[0] == 'F' && buf[1] == 'O' && buf[2] == 'R' && buf[3] == 'M'
	  && buf[8] == 'I' && buf[9] == 'F' && buf[10] == 'R' && buf[11] == 'S') {
		/* Load game from a chunk in the Blorb file. */
		locate_gamefile(TRUE);
		return TRUE;
	}
	return FALSE;
}



void glk_main() {
	Starlane::Frontend fe {
		.randomSeed = 0,
		.timersAvailable = !!glk_gestalt(gestalt_Timer, 0),
		.FatalError = &FatalError,
		.OutputText = &OutputText,
		.StrToUpperCase = &StrToUpperCase,
		.StrToLowerCase = &StrToLowerCase,
		.StrToSentenceCase = &StrToSentenceCase,
		.AskYesNo = &AskYesNoQuestion,
		.QuitGame = &glk_exit,
		.CreateSaveFile = &CreateSaveFile,
		.OpenSaveFile = &OpenSaveFile,
		.ReadFile = &ReadFile,
		.WriteFile = &WriteFile,
		.CloseFile = &CloseFile
	};
	Starlane::InitBackend(&fe);

	garglk_set_program_name("Starlane");

	// Style hints must be set before the windows they apply to are opened. We repurpose
	// BlockQuote for centered text, mirroring FrankenDrift's GlkRunner -- it's the closest
	// stock Glk style to what ADRIFT's `<center>` tag asks for.
	glk_stylehint_set(wintype_AllTypes, style_BlockQuote, stylehint_Justification, stylehint_just_Centered);

	gMainWin = glk_window_open(nullptr, 0, 0, wintype_TextBuffer, 0);
	if (!gMainWin) glk_exit();
	gMainStream = glk_window_get_stream(gMainWin);
	gStatusWin = glk_window_open(gMainWin, winmethod_Above | winmethod_Fixed, 1, wintype_TextGrid, 0);

	if (fe.timersAvailable) glk_request_timer_events(1000);

	gImagesSupported = glk_gestalt(gestalt_Graphics, 0) != 0 && glk_gestalt(gestalt_DrawImage, wintype_TextBuffer) != 0;
	gSoundSupported = glk_gestalt(gestalt_Sound2, 0) != 0;
	if (gSoundSupported) {
		for (int i = 1; i <= 8; i++) gSoundChannels[i] = glk_schannel_create((glui32) i);
	}

	if (init_err) {
		OutputStyled(init_err, kStyleBold);
		WaitForKeypress();
		glk_exit();
	}

	glk_stream_set_position(gamefile, gamefile_start, seekmode_Start);
	std::vector<uint8_t> tafData(gamefile_len);
	glk_get_buffer_stream(gamefile, reinterpret_cast<char *>(tafData.data()), gamefile_len);
	// For a Blorb game, `gamefile` is the memory stream backing the Blorb resource map (see
	// locate_gamefile()) -- it needs to stay open for the whole session, since the Glk library
	// reads from it on demand whenever the game asks for an image or sound.
	if (!giblorb_get_resource_map())
		glk_stream_close(gamefile, nullptr);

	Starlane::CreateGame(tafData.data(), tafData.size());
	Starlane::BeginGame();

	while (Starlane::GameIsOngoing()) {
		OutputStyled("\n> ", kStyleInput);
		std::string cmd = GetLineInput();
		Starlane::ProcessInput(cmd);
	}

	OutputStyled("\n[Press any key to exit.]", kStyleNormal);
	WaitForKeypress();
	glk_exit();
}
}

// TODO: probably move these to separate files as it makes sense.
void FatalError(const char *msg) {
	OutputStyled("\n", kStyleNormal);
	OutputStyled(msg, kStyleBold);
	OutputStyled("\n", kStyleNormal);
}

void OutputText(const char *msg) {
	AppendHtml(msg);
}

namespace {

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

// Removes the last complete UTF-8 codepoint from `s` (for the `<del>` tag), rather than just the
// last byte, so we never leave a dangling continuation byte behind.
void Utf8PopBack(std::string &s) {
	if (s.empty()) return;
	size_t i = s.size() - 1;
	while (i > 0 && (((unsigned char) s[i]) & 0xC0) == 0x80) i--;
	s.erase(i);
}

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

// The Unicode text of the most recently *flushed* OutputStyled() call, tracked so that `<del>`
// has something to try to unput once the current (not-yet-flushed) run is already empty.
std::vector<uint32_t> gMostRecentOutput;

// Locates the [start, end) byte range of `attr`'s value within `tagLower` (a lowercased tag
// body), handling both `attr="quoted value"` and `attr=unquoted` forms. Returns false if the
// attribute isn't present. ASCII case-folding never changes a string's length, so the range
// found here can be sliced out of either the lowercased tag body or the original one.
bool FindAttributeRange(const std::string &tagLower, const std::string &attr, size_t &start, size_t &end) {
	size_t pos = tagLower.find(attr);
	if (pos == std::string::npos) return false;
	pos += attr.size();
	while (pos < tagLower.size() && std::isspace((unsigned char) tagLower[pos])) pos++;
	if (pos >= tagLower.size() || tagLower[pos] != '=') return false;
	pos++;
	while (pos < tagLower.size() && std::isspace((unsigned char) tagLower[pos])) pos++;
	if (pos < tagLower.size() && (tagLower[pos] == '"' || tagLower[pos] == '\'')) {
		char quote = tagLower[pos++];
		size_t e = tagLower.find(quote, pos);
		if (e == std::string::npos) e = tagLower.size();
		start = pos;
		end = e;
	} else {
		size_t e = pos;
		while (e < tagLower.size() && !std::isspace((unsigned char) tagLower[e]) && tagLower[e] != '>') e++;
		start = pos;
		end = e;
	}
	return true;
}

// Extracts the value of `attr="..."` or `attr=...` from `tagLower`, a lowercased tag body.
// Returns false if the attribute isn't present.
bool ExtractAttribute(const std::string &tagLower, const std::string &attr, std::string &valueOut) {
	size_t start, end;
	if (!FindAttributeRange(tagLower, attr, start, end)) return false;
	valueOut = tagLower.substr(start, end - start);
	return true;
}

// Same as ExtractAttribute(), but returns the value with its original casing preserved (from
// `tagOriginal`, the same tag body before lowercasing) -- needed for `src` paths, which have to
// match ADRIFT's file-mapping keys exactly as the author wrote them.
bool ExtractAttributeOriginalCase(const std::string &tagLower, const std::string &tagOriginal,
                                   const std::string &attr, std::string &valueOut) {
	size_t start, end;
	if (!FindAttributeRange(tagLower, attr, start, end)) return false;
	valueOut = tagOriginal.substr(start, end - start);
	return true;
}

// Parses a `<font color="...">` attribute, either a `#`-optional hex triplet (e.g. the game data
// seen in the wild uses both `color = red` and `color = FDD017`) or one of the named colors
// ADRIFT's own runner recognizes (per FrankenDrift's GlkHtmlWin.cs, cross-referenced against the
// original VB source).
bool ParseFontColor(const std::string &tagLower, uint32_t &colorOut) {
	std::string value;
	if (!ExtractAttribute(tagLower, "color", value)) return false;
	if (!value.empty() && value[0] == '#') value = value.substr(1);
	bool isHex = value.size() == 6 && std::all_of(value.begin(), value.end(), [](char c) {
		return std::isxdigit((unsigned char) c) != 0;
	});
	if (isHex) {
		colorOut = (uint32_t) std::strtoul(value.c_str(), nullptr, 16);
		return true;
	}
	static const std::unordered_map<std::string, uint32_t> kNamedColors = {
		{ "black", 0x000000 }, { "blue", 0x0000ff }, { "gray", 0x808080 }, { "grey", 0x808080 },
		{ "darkgreen", 0x006400 }, { "green", 0x008000 }, { "lime", 0x00ff00 }, { "magenta", 0xff00ff },
		{ "maroon", 0x800000 }, { "navy", 0x000080 }, { "olive", 0x808000 }, { "orange", 0xffa500 },
		{ "pink", 0xffc0cb }, { "purple", 0x800080 }, { "red", 0xff0000 }, { "silver", 0xc0c0c0 },
		{ "teal", 0x008080 }, { "white", 0xffffff }, { "yellow", 0xffff00 }, { "cyan", 0x00ffff },
		{ "darkolive", 0x556b2f }, { "tan", 0xd2b48c },
	};
	auto it = kNamedColors.find(value);
	if (it == kNamedColors.end()) return false;
	colorOut = it->second;
	return true;
}

// Whether `face` (already lowercased) names one of the fonts commonly used to ask for a
// fixed-width look, so a `<font face="...">` without an explicit `<tt>`-like tag still gets
// mapped onto style_Preformatted. List cross-referenced from FrankenDrift's GlkHtmlWin.cs.
bool IsMonospaceFace(const std::string &face) {
	static const std::unordered_set<std::string> kMonospaceFaces = {
		"andale mono", "cascadia code", "century schoolbook monospace", "consolas", "courier",
		"courier new", "liberation mono", "ubuntu mono", "dejavu sans mono", "droid sans mono",
		"lucida console", "menlo", "ocr-a", "ocr-a extended", "overpass mono", "oxygen mono",
		"roboto mono", "source code pro", "everson mono", "fira mono", "fixed", "fixedsys",
		"freemono", "go mono", "hyperfont", "ibm mda", "ibm plex mono", "inconsolata", "iosevka",
		"letter gothic", "monaco", "monofur", "monospace", "monospace (unicode)", "nimbus mono l",
		"noto mono", "nk57 monospace", "ocr-b", "pragmatapro", "prestige elite", "profont",
		"pt mono", "spleen", "terminus", "tex gyre cursor", "american typewriter", "tads-monospace",
	};
	return kMonospaceFaces.count(face) > 0;
}

// Draws the image at `path` (an author-side file path, resolved through starlane-core's blorb
// file mapping) scaled to fill 100% of either the window's width or height, whichever is the
// smaller scale factor -- i.e. as large as possible without spilling outside either dimension.
void DrawImageFitted(const std::string &path) {
	if (!gImagesSupported) return;
	uint32_t resourceId = Starlane::GetBlorbResourceForPath(path);
	if (resourceId == (uint32_t) -1) return;

	glui32 imgWidth = 0, imgHeight = 0;
	if (!glk_image_get_info(resourceId, &imgWidth, &imgHeight) || imgWidth == 0 || imgHeight == 0)
		return;

	glui32 winWidth = 0, winHeight = 0;
	garglk_window_get_size_pixels(gMainWin, &winWidth, &winHeight);
	if (winWidth == 0 || winHeight == 0) {
		// No pixel-size extension available (e.g. the built-in cheapglk); fall back to drawing
		// at the image's native size rather than not scaling it at all.
		glk_image_draw(gMainWin, resourceId, imagealign_InlineCenter, 0);
	} else {
		double scale = std::min((double) winWidth / imgWidth, (double) winHeight / imgHeight);
		glk_image_draw_scaled(gMainWin, resourceId, imagealign_InlineCenter, 0,
		                       (glui32) (imgWidth * scale), (glui32) (imgHeight * scale));
	}
	glk_window_flow_break(gMainWin);
}

// `channel` is assumed already validated to be within [1, 8] -- AppendHtml, the only caller, is
// the boundary where that untrusted tag content gets checked.

void PlaySound(const std::string &path, int channel, bool loop) {
	if (!gSoundSupported) return;
	if (gRecentlyPlayedSound[channel] == path) {
		// Already the current sound on this channel: resume rather than restart from the top.
		glk_schannel_unpause(gSoundChannels[channel]);
		return;
	}
	uint32_t resourceId = Starlane::GetBlorbResourceForPath(path);
	if (resourceId == (uint32_t) -1) return;
	gRecentlyPlayedSound[channel] = path;
	glk_schannel_play_ext(gSoundChannels[channel], resourceId, loop ? 0xFFFFFFFFu : 1u, 0);
}

void PauseSound(int channel) {
	if (!gSoundSupported) return;
	glk_schannel_pause(gSoundChannels[channel]);
}

void StopSound(int channel) {
	if (!gSoundSupported) return;
	glk_schannel_stop(gSoundChannels[channel]);
	gRecentlyPlayedSound[channel].clear();
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

bool AskYesNoQuestion(const char *question) {
	OutputStyled("\n", kStyleNormal);
	OutputStyled(question, kStyleNormal);
	for (;;) {
		OutputStyled("\n[yes/no] > ", kStyleInput);
		std::string answer = StrToLowerCase(GetLineInput());
		if (answer == "y" || answer == "yes") return true;
		if (answer == "n" || answer == "no") return false;
	}
}

void OutputStyled(const std::string &text, uint32_t styleFlags, uint32_t color) {
	if (text.empty()) return;
	glui32 style;
	if (styleFlags & kStyleMonospace) style = style_Preformatted;
	else if (styleFlags & kStyleCentered) style = style_BlockQuote;
	else if ((styleFlags & kStyleBold) && (styleFlags & kStyleItalic)) style = style_Alert;
	else if (styleFlags & kStyleItalic) style = style_Emphasized;
	else if (styleFlags & kStyleBold) style = style_Subheader;
	else if (styleFlags & kStyleInput) style = style_Input;
	else style = style_Normal;

	// Harmless no-op if the underlying Glk library doesn't support the garglk color extension.
	garglk_set_zcolors_stream(gMainStream, color, zcolor_Default);
	glk_set_style_stream(gMainStream, style);
	auto codepoints = Utf8ToUtf32(text);
	glk_put_buffer_stream_uni(gMainStream, (glui32 *) codepoints.data(), (glui32) codepoints.size());
	gMostRecentOutput = std::move(codepoints);
}

void UnputLastChar() {
	if (gMostRecentOutput.empty()) return;
	glui32 buf[2] = { gMostRecentOutput.back(), 0 };
	glk_set_window(gMainWin);
	if (garglk_unput_string_count_uni(buf) > 0)
		gMostRecentOutput.pop_back();
}

void WaitForKeypress() {
	glk_request_char_event(gMainWin);
	event_t ev;
	for (;;) {
		glk_select(&ev);
		if (ev.type == evtype_CharInput && ev.win == gMainWin) return;
		if (ev.type == evtype_Timer) Starlane::TimeTick();
	}
}

std::string GetLineInput() {
	constexpr glui32 kCapacity = 256;
	std::vector<glui32> buf(kCapacity);
	glk_request_line_event_uni(gMainWin, buf.data(), kCapacity, 0);
	event_t ev;
	for (;;) {
		glk_select(&ev);
		if (ev.type == evtype_LineInput && ev.win == gMainWin) {
			return Utf32ToUtf8(buf.data(), ev.val1);
		}
		if (ev.type == evtype_Timer) Starlane::TimeTick();
	}
}

void AppendHtml(const std::string &html) {
	if (html.empty()) return;

	struct StyleFrame {
		uint32_t flags;
		uint32_t color;
		std::string tag;
	};
	std::vector<StyleFrame> styleStack;
	styleStack.push_back({ kStyleNormal, zcolor_Default, "<base>" });

	std::string current;
	bool inTag = false;
	std::string tagBuf;

	auto flush = [&]() {
		if (!current.empty()) {
			OutputStyled(current, styleStack.back().flags, styleStack.back().color);
			current.clear();
		}
	};

	for (char c : html) {
		if (c == '<' && !inTag) {
			inTag = true;
			tagBuf.clear();
		} else if (inTag && c != '>') {
			tagBuf += c;
		} else if (inTag && c == '>') {
			inTag = false;
			std::string tagLower = tagBuf;
			for (char &tc : tagLower) tc = (char) std::tolower((unsigned char) tc);
			std::string tagWord = tagLower.substr(0, tagLower.find_first_of(" \t"));

			if (tagWord == "del") {
				// Prefer removing from the not-yet-flushed run; only fall back to trying to
				// unput already-committed output (via the garglk extension) once that's empty.
				if (!current.empty()) Utf8PopBack(current);
				else UnputLastChar();
				continue;
			}

			flush();
			uint32_t curFlags = styleStack.back().flags;
			uint32_t curColor = styleStack.back().color;
			if (tagWord == "br") {
				OutputStyled("\n", curFlags, curColor);
			} else if (tagWord == "cls") {
				styleStack.assign(1, StyleFrame{ kStyleNormal, zcolor_Default, "<base>" });
				glk_window_clear(gMainWin);
			} else if (tagWord == "waitkey") {
				WaitForKeypress();
			} else if (tagWord == "b") {
				styleStack.push_back({ curFlags | kStyleBold, curColor, "b" });
			} else if (tagWord == "i") {
				styleStack.push_back({ curFlags | kStyleItalic, curColor, "i" });
			} else if (tagWord == "u") {
				// Glk has no underline style; tracked only so `</u>` balances the stack.
				styleStack.push_back({ curFlags, curColor, "u" });
			} else if (tagWord == "c") {
				styleStack.push_back({ curFlags | kStyleInput, curColor, "c" });
			} else if (tagWord == "center" || tagWord == "centre") {
				styleStack.push_back({ curFlags | kStyleCentered, curColor, "center" });
			} else if (tagWord == "left" || tagWord == "right") {
				// No distinct Glk alignment for these; tracked only so the closing tag balances.
				styleStack.push_back({ curFlags, curColor, tagWord });
			} else if (tagWord == "font") {
				// A `<font>` tag without a `color` attribute (e.g. one that only sets `size`)
				// keeps whatever color is already in effect, rather than resetting to default.
				uint32_t newFlags = curFlags;
				uint32_t newColor = curColor;
				uint32_t parsedColor;
				if (ParseFontColor(tagLower, parsedColor)) newColor = parsedColor;
				std::string face;
				if (ExtractAttribute(tagLower, "face", face) && IsMonospaceFace(face))
					newFlags |= kStyleMonospace;
				styleStack.push_back({ newFlags, newColor, "font" });
			} else if (!tagWord.empty() && tagWord[0] == '/') {
				std::string closeName = tagWord.substr(1);
				if (closeName == "centre") closeName = "center";
				if (styleStack.size() > 1 && styleStack.back().tag == closeName)
					styleStack.pop_back();
			} else if (tagWord == "img") {
				std::string src;
				if (ExtractAttributeOriginalCase(tagLower, tagBuf, "src", src) && !src.empty())
					DrawImageFitted(src);
			} else if (tagLower.rfind("audio play", 0) == 0) {
				std::string src;
				if (ExtractAttributeOriginalCase(tagLower, tagBuf, "src", src) && !src.empty()) {
					int channel = 1;
					std::string channelStr;
					if (ExtractAttribute(tagLower, "channel", channelStr))
						channel = std::atoi(channelStr.c_str());
					if (channel >= 1 && channel <= 8)
						PlaySound(src, channel, tagLower.find("loop=y") != std::string::npos);
				}
			} else if (tagLower.rfind("audio pause", 0) == 0) {
				int channel = 1;
				std::string channelStr;
				if (ExtractAttribute(tagLower, "channel", channelStr))
					channel = std::atoi(channelStr.c_str());
				if (channel >= 1 && channel <= 8) PauseSound(channel);
			} else if (tagLower.rfind("audio stop", 0) == 0) {
				int channel = 1;
				std::string channelStr;
				if (ExtractAttribute(tagLower, "channel", channelStr))
					channel = std::atoi(channelStr.c_str());
				if (channel >= 1 && channel <= 8) StopSound(channel);
			}
			// Anything else is not (yet) recognized and is silently dropped.
		} else {
			current += c;
		}
	}
	flush();
}

void *CreateSaveFile() {
	auto ref = glk_fileref_create_by_prompt(fileusage_SavedGame | fileusage_BinaryMode, filemode_Write, 0);
	if (!ref) return nullptr;
	auto file = glk_stream_open_file(ref, filemode_Write, 0);
	glk_fileref_destroy(ref);
	if (!file) {
		return nullptr;
	}
	return file;
}

void *OpenSaveFile() {
	auto ref = glk_fileref_create_by_prompt(fileusage_SavedGame | fileusage_BinaryMode, filemode_Read, 0);
	if (!ref) return nullptr;
	auto file = glk_stream_open_file(ref, filemode_Read, 0);
	glk_fileref_destroy(ref);
	if (!file) {
		return nullptr;
	}
	return file;
}

size_t ReadFile(void *hFile, uint8_t *buffer, size_t bufsize) {
	if (bufsize == 0) return 0;
	if constexpr (sizeof(bufsize) > sizeof(glui32)) {  //NOLINT
		if (bufsize > UINT32_MAX)
			bufsize = UINT32_MAX;
	}
	auto *file = reinterpret_cast<strid_t>(hFile);
	return glk_get_buffer_stream(file, reinterpret_cast<char *>(buffer), (glui32) bufsize);
}

void WriteFile(void *hFile, const uint8_t *buffer, size_t count) {
	if (count == 0) return;
	if constexpr (sizeof(count) > sizeof(glui32)) {  //NOLINT
		// yes, for 64-bit platforms this condition is always true. that's why it's marked `constexpr`...
		// (not that there should ever be anywhere near this much data, anyways)
		if (count > UINT32_MAX)
			throw std::out_of_range("starlane-glk WriteFile: too much data.");
	}
	auto *file = reinterpret_cast<strid_t>(hFile);
	char *mybuffer = new char[count];
	memcpy(mybuffer, buffer, count);
	glk_put_buffer_stream(file, mybuffer, (glui32) count);
	delete[] mybuffer;
}

void CloseFile(void *hFile) {
	auto file = reinterpret_cast<strid_t>(hFile);
	glk_stream_close(file, NULL);
}
