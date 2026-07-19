//
// Created by Adrian Welcker on 17.07.26.
//

#include <string.h>

#include <vector>

static constexpr uint8_t V5ident[] = { 60, 66, 63, 201, 106, 135, 194, 207, 146, 69, 62, 97 };

#include "starlane-glk-internal.h"

// Declared with C++ linkage (matching starlane-glk-internal.h's `extern` declarations) even
// though they're only ever touched from within the extern "C" block below.
winid_t gMainWin;
winid_t gStatusWin;
strid_t gMainStream;

extern "C" {
#include "gi_blorb.h"
#include "glkstart.h"

static strid_t gamefile;
static unsigned int gamefile_start, gamefile_len;
static const char *init_err;

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

	InitMultimedia();

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
