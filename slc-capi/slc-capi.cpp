//
// Created by Adrian Welcker on 19.06.23.
//

#include "slc-capi.h"
#include <starlane-core.h>

#include <stdlib.h>

#define SLCWRAP_ALLOC(size) ((cfe && cfe->alloc_func) ? cfe->alloc_func : malloc)(size)
#define SLCWRAP_DEALLOC(size) ((cfe && cfe->free_func) ? cfe->free_func : free)(size)

#ifdef __clang__
#define TAILCALL [[clang::musttail]]
#endif

static const slc__frontend *cfe = nullptr;
static Starlane::Frontend *fe = nullptr;

namespace Wrap {

void FatalError(const char *msg) {
	if (!cfe || !cfe->fatal_error) return;
	TAILCALL return (cfe->fatal_error)(msg);
}

void OutputText(const char *msg) {
	if (!cfe || !cfe->output_text) return;
	TAILCALL return (cfe->output_text)(msg);
}

#ifdef __GNUC__
#define StringChangeImpl(str_, func) ((func) ? StringChangeImpl2((str_), (func)) : str_)
#define StringChangeImpl2(str_, func) ({     \
	auto result_cstr = func((str_).c_str());   \
	std::string result(result_cstr);         \
	SLCWRAP_DEALLOC((void *) result_cstr);   \
	result;                                  \
})
#else
static inline std::string StringChangeImpl(const std::string &str, slc__stringchange func) {
	if (!func) return str;
	auto result_cstr = func(str.c_str());
	std::string result(result_cstr);
	SLCWRAP_DEALLOC((void *) result_cstr);
	return result;
}
#endif

std::string StrToUpperCase(const std::string &str) {
	return StringChangeImpl(str, cfe->str_to_upper_case);
}

std::string StrToLowerCase(const std::string &str) {
	return StringChangeImpl(str, cfe->str_to_lower_case);
}

std::string StrToSentenceCase(const std::string &str) {
	return StringChangeImpl(str, cfe->str_to_sentence_case);
}

void *CreateSaveFile() {
	if (!cfe->create_save_file) return nullptr;
	TAILCALL return (cfe->create_save_file)();
}

void *OpenSaveFile() {
	if (!cfe->create_save_file) return nullptr;
	TAILCALL return (cfe->open_save_file)();
}

size_t ReadFile(void *handle, uint8_t *buffer, size_t bufsize) {
	if (!cfe->read_file) return 0;
	TAILCALL return (cfe->read_file)(handle, buffer, bufsize);
}

void WriteFile(void *handle, const uint8_t *buffer, size_t count) {
	if (!cfe->write_file) return;
	TAILCALL return (cfe->write_file)(handle, buffer, count);
}

void CloseFile(void *handle) {
	if (!cfe->close_file) return;
	TAILCALL return (cfe->close_file)(handle);
}

}

void slc__init_backend(const slc__frontend *settings) {
	cfe = settings;
	fe = (Starlane::Frontend *) SLCWRAP_ALLOC(sizeof(Starlane::Frontend));
	fe->randomSeed = cfe->random_seed;
	fe->timersAvailable = cfe->timers_available;
	fe->FatalError = &Wrap::FatalError;
	fe->OutputText = &Wrap::OutputText;
	fe->StrToUpperCase = &Wrap::StrToUpperCase;
	fe->StrToLowerCase = &Wrap::StrToLowerCase;
	fe->StrToSentenceCase = &Wrap::StrToSentenceCase;
	fe->CreateSaveFile = &Wrap::CreateSaveFile;
	fe->OpenSaveFile = &Wrap::OpenSaveFile;
	fe->ReadFile = &Wrap::ReadFile;
	fe->WriteFile = &Wrap::WriteFile;
	fe->CloseFile = &Wrap::CloseFile;
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

char *slc__extract_taf(const uint8_t *input, size_t size) {
	auto content = Starlane::ExtractTaf(input, size);
	auto result = (char *) SLCWRAP_ALLOC(content.size() + 1);
	memcpy(result, content.c_str(), content.size() + 1);
	return result;
}

bool slc__game_is_ongoing() {
	TAILCALL return Starlane::GameIsOngoing();
}

uint32_t slc__get_blorb_resource_for_path(const char *path) {
	std::string p(path);
	return Starlane::GetBlorbResourceForPath(p);
}