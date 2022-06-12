#pragma once

#ifndef SLC_RANDOM_H
#define SLC_RANDOM_H

#include <stdint.h>

namespace Starlane {
void SeedRNG();
void SeedRNG(uint32_t seed);

// Get a random number from 0 up to `max`, both inclusive
uint32_t RandomInt(uint32_t max);
// Get a random number from `min` up to `max`, both inclusive.
uint32_t RandomInt(uint32_t min, uint32_t max);
}

#endif  // !SLC_RANDOM_H
