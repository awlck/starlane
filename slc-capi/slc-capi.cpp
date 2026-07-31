//
// Created by Adrian Welcker on 19.06.23.
//

#include "slc-capi.h"
#include <starlane-core.h>

#include <stdlib.h>
#include <string.h>
#include <stdexcept>

#define SLCWRAP_ALLOC(size) ((cfe && cfe->alloc_func) ? cfe->alloc_func : malloc)(size)
#define SLCWRAP_DEALLOC(size) ((cfe && cfe->free_func) ? cfe->free_func : free)(size)

static const slc__frontend *cfe = nullptr;
static Starlane::Frontend *fe = nullptr;

static char *DupString(const std::string &str) {
	auto size = str.size();
	auto result = (char *) SLCWRAP_ALLOC(size + 1);
	memcpy(result, str.c_str(), size);
	result[size] = 0;
	return result;
}

namespace Wrap {

static inline std::string StringChangeImpl(const std::string &str, slc__stringchange func) {
	if (!func) return str;
	auto result_cstr = func(str.c_str());
	std::string result(result_cstr);
	SLCWRAP_DEALLOC((void *) result_cstr);
	return result;
}

std::string StrToUpperCase(const std::string &str) {
	return StringChangeImpl(str, cfe->str_to_upper_case);
}

std::string StrToLowerCase(const std::string &str) {
	return StringChangeImpl(str, cfe->str_to_lower_case);
}

std::string StrToSentenceCase(const std::string &str) {
	return StringChangeImpl(str, cfe->str_to_sentence_case);
}

}

void slc__init_backend(const slc__frontend *settings) {
	cfe = settings;
	fe = (Starlane::Frontend *) SLCWRAP_ALLOC(sizeof(Starlane::Frontend));
	fe->randomSeed = cfe->random_seed;
	fe->timersAvailable = cfe->timers_available;
	fe->FatalError = cfe->fatal_error;
	fe->OutputText = cfe->output_text;
	fe->StrToUpperCase = &Wrap::StrToUpperCase;
	fe->StrToLowerCase = &Wrap::StrToLowerCase;
	fe->StrToSentenceCase = &Wrap::StrToSentenceCase;
	fe->AskYesNo = cfe->ask_yes_no;
	fe->QuitGame = cfe->quit_game;
	fe->PumpEvents = cfe->pump_events;
	fe->CreateSaveFile = cfe->create_save_file;
	fe->OpenSaveFile = cfe->open_save_file;
	fe->ReadFile = cfe->read_file;
	fe->WriteFile = cfe->write_file;
	fe->CloseFile = cfe->close_file;
	return Starlane::InitBackend(fe);
}

#define ISSUE_ERROR_FROM(exc, stage) { \
	std::string msg = "Error " stage ": "; \
	msg.append(exc.what()); \
	cfe->fatal_error(msg.c_str()); \
}

void slc__create_game(const uint8_t *taf_bytes, size_t taf_length) {
	try {
		return Starlane::CreateGame(taf_bytes, taf_length);
	} catch (const std::runtime_error &e)
		ISSUE_ERROR_FROM(e, "loading game");
}

void slc__begin_game() {
	try {
		return Starlane::BeginGame();
	} catch (const std::runtime_error &e)
		ISSUE_ERROR_FROM(e, "starting game")
}

void slc__time_tick() {
	try {
		return Starlane::TimeTick();
	} catch (const std::runtime_error &e)
		ISSUE_ERROR_FROM(e, "ticking game clock");
}

void slc__process_input(const char *cmd) {
	std::string theCommand(cmd);
	try {
		return Starlane::ProcessInput(theCommand);
	} catch (const std::runtime_error &e)
		ISSUE_ERROR_FROM(e, "processing command");
}

bool slc__save_game() {
	try {
		return Starlane::SaveGame();
	} catch (const std::runtime_error &e)
		ISSUE_ERROR_FROM(e, "saving game");
	return false;
}

bool slc__restore_game() {
	try {
		return Starlane::RestoreGame();
	} catch (const std::runtime_error &e)
		ISSUE_ERROR_FROM(e, "restoring game");
	return false;
}

char *slc__extract_taf(const uint8_t *input, size_t size) {
	try {
		auto content = Starlane::ExtractTaf(input, size);
		auto resultsize = content.size();
		auto result = (char *) SLCWRAP_ALLOC(resultsize + 1);
		memcpy(result, content.c_str(), resultsize);
		result[resultsize] = 0;
		return result;
	} catch (const std::runtime_error &e)
		ISSUE_ERROR_FROM(e, "extracting TAF file");
	return nullptr;
}

bool slc__game_is_ongoing() {
	return Starlane::GameIsOngoing();
}

uint32_t slc__get_blorb_resource_for_path(const char *path) {
	std::string p(path);
	return Starlane::GetBlorbResourceForPath(p);
}

bool slc__get_status_bar(slc__status_bar *status_bar) {
	Starlane::StatusBar sb;
	auto ok = Starlane::GetStatusBar(sb);
	if (!ok) return false;
	status_bar->location = DupString(sb.location);
	status_bar->user_status = DupString(sb.userStatus);
	status_bar->score = sb.score;
	status_bar->scoring_used = sb.scoringUsed;
	return true;
}

bool slc__get_game_info(slc__game_info *info) {
	Starlane::GameInfo gi;
	auto ok = Starlane::GetGameInfo(&gi);
	if (!ok) return false;
	info->title = DupString(gi.title);
	info->author = DupString(gi.author);
	info->font_name = DupString(gi.fontName);
	info->has_input_colour = gi.hasInputColour;
	info->input_colour = gi.inputColour;
	info->has_output_colour = gi.hasOutputColour;
	info->output_colour = gi.outputColour;
	return true;
}
