#pragma once

#ifndef SLC_VALUEPARSERS_H
#define SLC_VALUEPARSERS_H

#include <stdexcept>
#include <utility>

namespace Starlane {

bool ParseBool(const char *txt);
int64_t ParseInt(const char *txt);

}

#endif  // !SLC_VALUEPARSERS_H
