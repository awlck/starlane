#pragma once

#ifndef SLC_TAFFILE_H
#define SLC_TAFFILE_H

#include <string>

namespace Starlane {
std::string ExtractTaf(const uint8_t *input, size_t size);
}

#endif  // !SLC_TAFFILE_H