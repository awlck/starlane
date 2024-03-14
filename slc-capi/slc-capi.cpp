//
// Created by Adrian Welcker on 19.06.23.
//

#include "slc-capi.h"
#include <starlane-core.h>

#include <stdlib.h>
#include <string.h>

#define SLCWRAP_ALLOC(size) ((cfe && cfe->alloc_func) ? cfe->alloc_func : malloc)(size)
#define SLCWRAP_DEALLOC(size) ((cfe && cfe->free_func) ? cfe->free_func : free)(size)

#ifdef __clang__
#define TAILCALL [[clang::musttail]]
#else
#define TAILCALL
#endif

static const slc__frontend *cfe = nullptr;
static Starlane::Frontend *fe = nullptr;

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
	fe->CreateSaveFile = cfe->create_save_file;
	fe->OpenSaveFile = cfe->open_save_file;
	fe->ReadFile = cfe->read_file;
	fe->WriteFile = cfe->write_file;
	fe->CloseFile = cfe->close_file;
	return Starlane::InitBackend(fe);
}

void slc__create_game(const uint8_t *taf_bytes, size_t taf_length) {
	TAILCALL return Starlane::CreateGame(taf_bytes, taf_length);
}

void slc__begin_game() {
	TAILCALL return Starlane::BeginGame();
}

void slc__time_tick() {
	TAILCALL return Starlane::TimeTick();
}

void slc__process_input(const char *cmd) {
	std::string theCommand(cmd);
	return Starlane::ProcessInput(theCommand);
}

bool slc__save_game() {
    TAILCALL return Starlane::SaveGame();
}

bool slc__restore_game() {
	TAILCALL return Starlane::RestoreGame();
}

char *slc__extract_taf(const uint8_t *input, size_t size) {
	auto content = Starlane::ExtractTaf(input, size);
    auto resultsize = content.size();
	auto result = (char *) SLCWRAP_ALLOC(resultsize + 1);
	memcpy(result, content.c_str(), resultsize);
    result[resultsize] = 0;
	return result;
}

bool slc__game_is_ongoing() {
	TAILCALL return Starlane::GameIsOngoing();
}

uint32_t slc__get_blorb_resource_for_path(const char *path) {
	std::string p(path);
	return Starlane::GetBlorbResourceForPath(p);
}
