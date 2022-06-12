#include "starlane-core.h"

#include "game.h"
#include "random.h"

namespace Starlane {

void InitBackend() {
	SeedRNG();
}

void TimeTick() {
	auto theGame = Game::Get();
	if (theGame)
		theGame->Tick();
}

}