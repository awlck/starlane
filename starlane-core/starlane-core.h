#pragma once

#ifndef SLC_STARLANE_CORE_H
#define SLC_STARLANE_CORE_H

#include <stdint.h>

namespace SLFrontend {
// Show a fatal error message. The frontend should refuse any further input
// after a fatal error has been issued.
void FatalError(const char *msg);
}

namespace Starlane {

// Frontend capabilities and settings
struct FECapabilities {
	// Seed for the random number generator, or zero for a random seed.
	uint32_t randomSeed = 0;
};

// Initialize the backend with the given settings.
void InitBackend(const FECapabilities &settings);

void BeginGame();
void TimeTick();
}


#endif  // !SLC_STARLANE_CORE_H