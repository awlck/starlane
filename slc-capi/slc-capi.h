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

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef const char *(*slc__stringchange)(const char *);
	typedef void (*slc__textoutput)(const char *);

	typedef struct _slc__frontend {
		void *(*alloc_func)(size_t size);
		void (*free_func)(void *block);

		uint32_t random_seed;
		bool timers_available;
		slc__textoutput output_text;
		slc__textoutput fatal_error;
		slc__stringchange str_to_upper_case;
		slc__stringchange str_to_lower_case;
		slc__stringchange str_to_sentence_case;

		void *(*create_save_file)();
		void *(*open_save_file)();
		size_t (*read_file)(void *handle, uint8_t *buffer, size_t bufsize);
		void (*write_file)(void *handle, const uint8_t *buffer, size_t count);
		void (*close_file)(void *handle);
	} slc__frontend;

	SLC_CAPI void slc__init_backend(const slc__frontend *frontend);
	SLC_CAPI void slc__create_game(const uint8_t *taf_bytes, size_t taf_length);
	SLC_CAPI void slc__begin_game();
	SLC_CAPI void slc__time_tick();
	SLC_CAPI bool slc__save_game();
	SLC_CAPI bool slc__restore_game();
	SLC_CAPI void slc__process_input(const char *cmd);

	SLC_CAPI char *slc__extract_taf(const uint8_t *input, size_t size);
	SLC_CAPI bool slc__game_is_ongoing();
	SLC_CAPI uint32_t slc__get_blorb_resource_for_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif  // !STARLANE_SLC_CAPI_H
