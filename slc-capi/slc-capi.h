//
// Created by Adrian Welcker on 19.06.23.
//

#pragma once

#ifndef STARLANE_SLC_CAPI_H
#define STARLANE_SLC_CAPI_H

#ifdef slc_capi_EXPORTS
#  ifdef _WIN32
#    define SLC_CAPI __declspec(dllexport)
#  else
#    define SLC_CAPI __attribute__((visibility("default")))
#  endif
#else
#  if defined(_WIN32) && defined(SL_SHARED_CORE)
#    define SLC_CAPI __declspec(dllimport)
#  else
#    define SLC_CAPI
#  endif
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	// Implementation must return a string allocated with the frontend's own alloc_func (the same
	// one passed via `slc__frontend::alloc_func`); slc-capi copies it and then releases it with
	// free_func before returning, so the frontend must not free it itself. The input pointer is
	// only valid for the duration of the call -- do not retain it.
	typedef const char *(*slc__stringchange)(const char *);
	// The text pointer is only valid for the duration of the call and is owned by starlane-core;
	// do not retain or free it.
	typedef void (*slc__textoutput)(const char *);

	typedef struct _slc__frontend {
		// Used by slc-capi (and by callback implementations, e.g. str_to_upper_case) for any
		// memory it hands back across the API boundary; must be a matched pair with free_func.
		void *(*alloc_func)(size_t size);
		void (*free_func)(void *block);

		uint32_t random_seed;
		bool timers_available;
		slc__textoutput output_text;
		slc__textoutput fatal_error;
		slc__stringchange str_to_upper_case;
		slc__stringchange str_to_lower_case;
		slc__stringchange str_to_sentence_case;
		// `question` is only valid for the duration of the call; do not retain or free it.
		bool (*ask_yes_no)(const char *question);
		void (*quit_game)();
		void (*pump_events)();

		// Return an opaque handle owned by the frontend; starlane-core never frees it directly,
		// only ever passing it back to read_file/write_file/close_file. close_file is always
		// eventually called to release it.
		void *(*create_save_file)();
		void *(*open_save_file)();
		// `buffer` is allocated by the caller (starlane-core) with capacity `bufsize`; the frontend
		// fills it but does not own it and must not free it.
		size_t (*read_file)(void *handle, uint8_t *buffer, size_t bufsize);
		// `buffer` is owned by the caller (starlane-core) and is only valid for the duration of the
		// call; the frontend must not retain or free it.
		void (*write_file)(void *handle, const uint8_t *buffer, size_t count);
		// Releases the handle (and whatever resources the frontend associated with it via
		// create_save_file/open_save_file). The handle is invalid for any further use afterwards.
		void (*close_file)(void *handle);
	} slc__frontend;

	// The `frontend` pointer is stowed away for the lifetime of the backend (e.g. callbacks like
	// str_to_upper_case are invoked through it well after this call returns), so the caller must
	// keep it -- and everything it points to -- alive and must not free it.
	SLC_CAPI void slc__init_backend(const slc__frontend *frontend);
	// `taf_bytes` is only needed for the duration of this call; the caller may free it as soon as
	// slc__create_game() returns.
	SLC_CAPI void slc__create_game(const uint8_t *taf_bytes, size_t taf_length);
	SLC_CAPI void slc__begin_game();
	SLC_CAPI void slc__time_tick();
	SLC_CAPI bool slc__save_game();
	SLC_CAPI bool slc__restore_game();
	// `cmd` is only needed for the duration of this call; the caller retains ownership and may
	// free it as soon as slc__process_input() returns.
	SLC_CAPI void slc__process_input(const char *cmd);

	// `input` is only needed for the duration of this call and remains owned by the caller. The
	// returned string is allocated with alloc_func; the caller must release it with free_func.
	SLC_CAPI char *slc__extract_taf(const uint8_t *input, size_t size);
	SLC_CAPI bool slc__game_is_ongoing();
	// `path` is only needed for the duration of this call and remains owned by the caller.
	SLC_CAPI uint32_t slc__get_blorb_resource_for_path(const char *path);

	typedef struct _slc__status_bar {
		char *location;
		char *user_status;
		int32_t score;
		bool scoring_used;
	} slc__status_bar;

	// Get the current status bar. Call this after every time you call
	// slc__begin_game(), slc__process_input(), or slc__time_tick().
	// `status_bar` itself is caller-owned (e.g. stack-allocated) and filled in by this call; the
	// `location`/`user_status` strings it receives are allocated with alloc_func, and the caller
	// must release them with free_func once done with them.
	SLC_CAPI bool slc__get_status_bar(slc__status_bar *status_bar);

	typedef struct _slc__game_info {
		char *title;
		char *author;
		// The author's preferred display font (<FontName>), or empty if unspecified.
		char *font_name;
		// The author's preferred input/output text colors (<InputColour>/<OutputColour>), packed
		// as 0xRRGGBB. has_input_colour/has_output_colour are false (and the color left at 0) if
		// the game does not specify one -- a frontend should fall back to its own default in that
		// case, rather than treating an absent color as black.
		bool has_input_colour;
		uint32_t input_colour;
		bool has_output_colour;
		uint32_t output_colour;
	} slc__game_info;

	// Get bibliographic/display info about the current game. Call any time after
	// slc__create_game(). `info` itself is caller-owned (e.g. stack-allocated) and filled in by
	// this call; the `title`/`author`/`font_name` strings it receives are allocated with
	// alloc_func, and the caller must release them with free_func once done with them.
	SLC_CAPI bool slc__get_game_info(slc__game_info *info);

#ifdef __cplusplus
}
#endif

#endif  // !STARLANE_SLC_CAPI_H
