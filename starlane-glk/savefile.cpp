//
// Created by Adrian Welcker on 17.07.26.
//

#include "starlane-glk-internal.h"

#include <cstring>
#include <stdexcept>

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
