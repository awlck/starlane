//
// Created by Adrian Welcker on 11.07.23.
//

#include "game.h"

#include <regex>

#include "gamecontent/description.h"

namespace Starlane {

void Game::ProcessInput(const std::string &s) {
	currentCommand = s;

	// TODO: deal with the two execution policies.
	// figure out which general task to apply, and whether it's currently possible to do so:
	Task *chosenTask;
	std::pair<bool, DescrRef> eligible;
	for (const auto &t: staticData->prioOrderedTasks) {
		if (t->GetType() != Task::Type::General) continue;
		for (const auto &rex: t->GetCmdRegexes()) {
			if (std::regex_match(currentCommand, rex)) {
				eligible = t->Eligible();
				// Choose the first (highest-priority) task that tentatively passes restrictions,
				// or otherwise fails restrictions but wants to output some text because of it.
				// Ignore tasks that fail restrictions and have no associated message.
				if (eligible.first || eligible.second != 0) {
					chosenTask = t;
					goto foundTask;
				}
			}
		}
	}

	// If we get here, the above loop ran through without finding any matching general task.
	// Attempt to read this as a system command ...
	if (AttemptMatchSystemCommand()) return;
	// ... and, failing that, reject the command as unknown.
	frontend->OutputText("I didn't understand that sentence.\n");
	return;

foundTask:
	// output failure message if restrictions failed
	if (!eligible.first) {
		if (eligible.second != 0) {
			std::string out(GetDescription(eligible.second)->Build());
			frontend->OutputText(out.c_str());
		} else {
			frontend->OutputText("You can't do that right now.");
		}
		return;
	}

	// Match the command again, this time taking care to capture the references within
	std::smatch matches;
	for (const auto &rex: chosenTask->GetCmdRegexes())
		if (std::regex_match(currentCommand, matches, rex)) break;
}

bool Game::AttemptMatchSystemCommand() {
	return false;
}

}  // namespace Starlane