#pragma once

#ifndef SLC_VALUEPARSERS_H
#define SLC_VALUEPARSERS_H

#include <stdexcept>
#include <utility>

namespace Starlane {

bool ParseBool(const char *txt);
int64_t ParseInt(const char *txt);

// Check whether the C-style string `txt` contains only digits.
bool IsDigits(const char *txt);

// Assuming that `input` starts with the string `toSkip`, get a pointer
// to the text following `toSkip` in `input`.
const char *SkipText(const char *input, const char *toSkip);

}

#endif  // !SLC_VALUEPARSERS_H
