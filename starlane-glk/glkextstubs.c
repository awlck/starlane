//
// Created by Adrian Welcker on 17.07.26.
//

/* Stubs for Glk extension functions that won't exist on some (or most) Glk implementations.
 * Since we compile our code as a static lib and the Glk implementation as the executable,
 * this file will only be loaded if the Glk implementation doesn't provide these functions.
 * See: https://devblogs.microsoft.com/oldnewthing/20130109-00/?p=5613
 * (If this turns out not to work in some instances, we'll have to contend with
 *  `__attribute__((weak))` on Linux, and
 *  `#pragma comment(linker,"/ALTERNATENAME:...)` on Windows.)
 */

#include "glkext.h"

void garglk_set_zcolors(glui32 fg, glui32 bg) {}
void garglk_set_zcolors_stream(strid_t str, glui32 fg, glui32 bg) {}

glui32 garglk_unput_string_count_uni(glui32* str) { return 0; }

// yes this is a totally normal thing to write, why do you ask?
// i like how glk is not quite universal enough to be useful so you keep doing shit like this:
// (Is this useful? I feel like Garglk and WinGlk are probably the only Glk libraries that
//  most Windows users would care about. If you really want to compile cheapglk or glkterm
//  on Windows then you already have a lot of patching ahead of you.)
#ifdef _MSC_VER
#pragma comment(linker, "/ALTERNATENAME:garglk_set_story_title=winglk_window_set_title")
#pragma comment(linker, "/ALTERNATENAME:garglk_set_program_name=winglk_app_set_name")
#else
__attribute__((weak)) void garglk_set_story_title(const char *title) {}
__attribute__((weak)) void garglk_set_program_name(const char *name) {}
#endif

