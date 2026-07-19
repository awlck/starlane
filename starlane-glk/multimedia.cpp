//
// Created by Adrian Welcker on 17.07.26.
//

#include "starlane-glk-internal.h"

#include <algorithm>

namespace {

// Whether images can be drawn in the main window, and whether sound is available at all --
// checked once at startup so <img>/<audio> handling can bail out early instead of going through
// a blorb lookup for nothing.
bool gImagesSupported;
bool gSoundSupported;
// ADRIFT supports 8 parallel audio channels, numbered 1-8; index 0 is unused.
schanid_t gSoundChannels[9];
// The path most recently (successfully) started on each channel, so replaying the exact same
// sound that's merely paused resumes it instead of restarting it from the top.
std::string gRecentlyPlayedSound[9];

}  // namespace

void InitMultimedia() {
	gImagesSupported = glk_gestalt(gestalt_Graphics, 0) != 0 && glk_gestalt(gestalt_DrawImage, wintype_TextBuffer) != 0;
	gSoundSupported = glk_gestalt(gestalt_Sound2, 0) != 0;
	if (gSoundSupported) {
		for (int i = 1; i <= 8; i++) gSoundChannels[i] = glk_schannel_create((glui32) i);
	}
}

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
