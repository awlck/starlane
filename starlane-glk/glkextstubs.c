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

#include "glk.h"

void garglk_set_zcolors(glui32 fg, glui32 bg) {}
void garglk_set_zcolors_stream(strid_t str, glui32 fg, glui32 bg) {}

glui32 garglk_unput_string_count_uni(glui32* str) { return 0; }