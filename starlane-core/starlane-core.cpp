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

void ProcessInput(const std::string &cmd) {
	auto theGame = Game::Get();
	if (theGame)
		theGame->ProcessInput(cmd);
}

bool GameIsOngoing() {
	return (Game::Get() && Game::Get()->IsGameOngoing());
}

uint32_t GetBlorbResourceForPath(const std::string &path) {
	auto *g = Game::Get();
	if (!g) return -1;
	return g->GetBlorbResource(path);
}

bool SaveGame() {
	auto *g = Game::Get();
	if (!g) return false;
	return g->Save();
}

bool RestoreGame() {
	auto *g = Game::Get();
	if (!g) return false;
	return g->Restore();
}

}
