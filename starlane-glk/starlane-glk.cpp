//
// Created by Adrian Welcker on 17.07.26.
//

#include <stdint.h>
#include <string.h>

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

extern "C" {
#include "glk.h"
#include "gi_blorb.h"
#include "glkstart.h"

static strid_t gamefile;
static unsigned int gamefile_start, gamefile_len;
static const char *init_err;

/* locate_gamefile:
   Given that gamefile contains a Glk stream, which may be a Glulx
   file or a Blorb archive containing one, locate the beginning and
   end of the Glulx data.
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
		/* A Blorb file. We now have to open it and find the Adrift chunk. */
		// TODO: Adrift blorb files are subtly invalid and will fail in most blorb loaders.
		giblorb_err_t err;
		giblorb_result_t blorbres;
		giblorb_map_t *map;

		err = giblorb_set_resource_map(gamefile);
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

	/* TODO:
	 * 1. load the TAF file from the `gamefile` stream (`gamefile_start` + `gamefile_len`).
	 * 2. call CreateGame
	 * 3. Create Glk main window and statusbar window
	 * 4. Call Game::Begin
     */
	event_t ev;
	while (TRUE) {
		glk_select(&ev);
		switch (ev.type) {
			default:
				break;
		}
	}
}
}

// TODO: probably move these to separate files as it makes sense.
void FatalError(const char *msg) {
	// TODO
}

void OutputText(const char *msg) {
	// TODO
}

std::string StrToUpperCase(const std::string &str) {
	// TODO: convert to unicode runes and call glk_buffer_to_upper_case_uni, then convert back to utf-8 std::string
	return str;
}

std::string StrToLowerCase(const std::string &str) {
	// TODO: convert to unicode runes and call glk_buffer_to_lower_case_uni, then convert back to utf-8 std::string
	return str;
}

std::string StrToSentenceCase(const std::string &str) {
	// TODO
	return str;
}

bool AskYesNoQuestion(const char *question) {
	// TODO
	return false;
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