#include "starlane-core.h"

#include "game.h"
#include "random.h"

namespace Starlane {

void InitBackend(const FECapabilities &settings) {
	SeedRNG(settings.randomSeed);
}

void TimeTick() {
	auto theGame = Game::Get();
	if (theGame)
		theGame->Tick();
}

}