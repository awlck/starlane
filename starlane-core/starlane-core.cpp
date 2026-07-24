#include "starlane-core.h"

#include <exception>
#include <string>

#include "miniz.h"

#include "error.h"
#include "game.h"
#include "random.h"

namespace Starlane {

const Frontend *frontend;

void InitBackend(const Frontend *fe) {
	frontend = fe;
	SeedRNG(frontend->randomSeed);
}

namespace {

// Log a caught, otherwise-unhandled exception to the developer log, appending its backtrace when
// it is one of ours (foreign exceptions carry none). Never shown to the player.
void LogCaught(const std::string &context, const std::exception &e) {
	std::string msg = context + ": " + e.what();
	if (const auto *se = dynamic_cast<const Exception *>(&e); se && !se->trace().empty())
		msg += "\n" + se->trace();
	LogError(msg);
}

}  // namespace

void CreateGame(const uint8_t *tafBytes, size_t tafLength) {
	// The whole load runs here. Genuinely malformed files that get past the per-restriction and
	// per-action faulty-marking (a structural problem, say) throw out of LoadFromXML; catch that
	// so the process survives, discard whatever half-built game was left, and tell the frontend.
	try {
		auto crc32Baseval = mz_crc32(0, NULL, 0);
		auto gameCrc32 = mz_crc32(crc32Baseval, tafBytes, tafLength);
		auto content = ExtractTaf(tafBytes, tafLength);
		Game::LoadFromXML(content, (uint32_t) gameCrc32);
	} catch (const std::exception &e) {
		LogCaught("Uncaught error while loading game", e);
		Game::Discard();
		frontend->FatalError("The game could not be loaded due to an internal error.");
	}
}

void BeginGame() {
	auto theGame = Game::Get();
	if (!theGame)
		return;
	try {
		theGame->Begin();
	} catch (const std::exception &e) {
		LogCaught("Uncaught error while starting game", e);
		Game::Discard();
		frontend->FatalError("The game could not be started due to an internal error.");
	}
}

void TimeTick() {
	auto theGame = Game::Get();
	if (!theGame)
		return;
	// A real-time tick advances the world just like a turn; catch anything it throws so the app
	// doesn't die, rolling the tick back if it managed to record a snapshot before failing.
	size_t undoBefore = Game::UndoDepth();
	try {
		theGame->Tick();
	} catch (const std::exception &e) {
		LogCaught("Uncaught error during real-time tick", e);
		if (Game::Get() && Game::UndoDepth() > undoBefore)
			Game::Get()->RestoreUndo();
	}
}

void ProcessInput(const std::string &cmd) {
	auto theGame = Game::Get();
	if (!theGame)
		return;
	// The undo stack depth as it stands before the turn: if the turn records a snapshot (it does so
	// just before running the matched task) and then throws, we roll back to that snapshot so the
	// half-applied turn doesn't stick. A throw from before the snapshot -- e.g. while merely testing
	// task restrictions, which mutate nothing -- leaves the world clean, so there is nothing to undo.
	size_t undoBefore = Game::UndoDepth();
	try {
		theGame->ProcessInput(cmd);
	} catch (const std::exception &e) {
		LogCaught("Uncaught error while processing input", e);
		// Get() may name a different instance than `theGame` now: a mid-turn RESTART/UNDO can swap
		// or delete the game before the throw. Operate only on whatever is current.
		bool rolledBack = false;
		if (Game::Get() && Game::UndoDepth() > undoBefore) {
			Game::Get()->RestoreUndo();
			rolledBack = true;
		}
		if (Game *now = Game::Get())
			now->OutputFiltered(rolledBack
				? "An internal error occurred; the last action was undone.\n"
				: "An internal error occurred.\n");
	}
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
