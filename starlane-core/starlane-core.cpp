#include "starlane-core.h"

#include "game.h"
#include "random.h"

namespace Starlane {

const Frontend *frontend;

void InitBackend(const Frontend *fe) {
	frontend = fe;
	SeedRNG(frontend->randomSeed);
}

void CreateGame(const uint8_t *tafBytes, size_t tafLength) {
	auto content = ExtractTaf(tafBytes, tafLength);
	Game::LoadFromXML(content);
}

void BeginGame() {
	auto theGame = Game::Get();
	if (theGame)
		theGame->Begin();
}

void TimeTick() {
	auto theGame = Game::Get();
	if (theGame)
		theGame->Tick();
}

bool GameIsOngoing() {
	return (Game::Get() && Game::Get()->IsGameOngoing());
}

}