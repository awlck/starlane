#include "random.h"

#include <random>

namespace Starlane {

namespace {
// Will be seeded by InitBackend()
std::mt19937 engine;  // NOLINT
}

void SeedRNG() {
	std::random_device osSeed;
	engine.seed(osSeed());
}

void SeedRNG(uint32_t seed) {
	if (seed == 0) {
		SeedRNG();
		return;
	} else engine.seed(seed);
}

uint32_t RandomInt(uint32_t max) {
	std::uniform_int_distribution<uint32_t> dist(0, max);
	return dist(engine);
}

uint32_t RandomInt(uint32_t min, uint32_t max) {
	std::uniform_int_distribution<uint32_t> dist(min, max);
	return dist(engine);
}

}