//
// Created by Adrian Welcker on 17.07.26.
//

#pragma once

#ifndef STARLANE_GLKEXT_H
#define STARLANE_GLKEXT_H

#include "glk.h"

// declarations of semi-common Glk extension functions that we use a lot
// and that will hopefully be automatically stubbed out if they turn out
// not to exist.
void garglk_set_zcolors(glui32 fg, glui32 bg);
void garglk_set_zcolors_stream(strid_t str, glui32 fg, glui32 bg);

glui32 garglk_unput_string_count_uni(const glui32* str);

void garglk_set_story_title(const char *title);
void garglk_set_program_name(const char *name);

#define zcolor_Transparent   ((glui32)0xfffffffc)
#define zcolor_Cursor        ((glui32)0xfffffffd)
#define zcolor_Current       ((glui32)0xfffffffe)
#define zcolor_Default       ((glui32)0xffffffff)

#endif //STARLANE_GLKEXT_H
