#include "starlane-core.h"

#include "miniz.h"

#include "game.h"
#include "random.h"

namespace Starlane {

const Frontend *frontend;

void InitBackend(const Frontend *fe) {
	frontend = fe;
	SeedRNG(frontend->randomSeed);
}

void CreateGame(const uint8_t *tafBytes, size_t tafLength) {
	auto crc32Baseval = mz_crc32(0, NULL, 0);
	auto gameCrc32 = mz_crc32(crc32Baseval, tafBytes, tafLength);
	auto content = ExtractTaf(tafBytes, tafLength);
	Game::LoadFromXML(content, (uint32_t) gameCrc32);
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
	// "Ongoing" from the frontend's point of view: whether it's still worth reading input at all.
	// Stays true across a Win/Lose/Neutral ending, since the player can still answer the final
	// question (RESTART/RESTORE/QUIT/UNDO) through ProcessInput; only QUIT clears it.
	return (Game::Get() && Game::Get()->IsSessionActive());
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

bool GetStatusBar(StatusBar *statusBar) {
	auto *g = Game::Get();
	if (!g) return false;
	return g->GetStatusBar(statusBar);
}
}
