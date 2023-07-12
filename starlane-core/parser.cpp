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
		if (std::regex_match(currentCommand, std::regex(t->GetCmdRegex()))) {
			eligible = t->Eligible();
			// Choose the first (highest-priority) task that tentatively passes restrictions,
			// or otherwise fails restrictions but wants to output some text because of it.
			// Ignore tasks that fail restrictions and have no associated message.
			if (eligible.first || eligible.second != 0) {
				chosenTask = t;
				break;
			} else continue;
		}
	}
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
}

}  // namespace Starlane