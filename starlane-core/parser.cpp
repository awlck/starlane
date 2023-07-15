//
// Created by Adrian Welcker on 11.07.23.
//

#include "game.h"

#include <regex>

#include "gamecontent/description.h"
#include "gamecontent/synonym.h"

namespace Starlane {

std::string Game::ApplySynonyms(const std::string &s) {
	std::string result(s);
	for (const auto &it: staticData->synonyms) {
		for (const auto &f: it.second->GetFrom()) {
			size_t n;
			while ((n = result.find(f)) != std::string::npos)
				result.replace(n, f.size(), it.second->GetReplacement());
		}
	}
	return result;
}

std::pair<bool, DescrRef> Game::SearchTaskFrom(typename decltype(GameStatic::prioOrderedTasks)::const_iterator &it) const {
	std::pair<bool, DescrRef> eligible;
	for (; it != staticData->prioOrderedTasks.cend(); it++) {
		if ((*it)->GetType() != Task::Type::General) continue;
		for (const auto &rex : (*it)->GetCmdRegexes()) {
			if (std::regex_match(currentCommand, rex)) {
				eligible = (*it)->Eligible();
				// Choose the first (highest-priority) task that tentatively passes restrictions,
				// or otherwise fails restrictions but wants to output some text because of it.
				// Ignore tasks that fail restrictions and have no associated message.
				if (eligible.first || eligible.second != 0) {
					return eligible;
				}
			}
		}
	}
	return { false, 0 };
}

void Game::ProcessInput(const std::string &s) {
	currentCommand = ApplySynonyms(s);

	// TODO: deal with the two execution policies.
	// figure out which general task to apply, and whether it's currently possible to do so:
	Task *chosenTask;
	auto taskIter = staticData->prioOrderedTasks.cbegin();

continueSearch:
	std::pair<bool, DescrRef> eligible = SearchTaskFrom(taskIter);
	if (taskIter == staticData->prioOrderedTasks.cend()) {
		// No match, attempt to read this as a system command ...
		if (AttemptMatchSystemCommand()) return;
		// ... and, failing that, reject the command as unknown.
		frontend->OutputText("I didn't understand that sentence.\n");
		return;
	} else {
		chosenTask = *taskIter;
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

	// Match the command again, this time taking care to capture the references within
	std::smatch matches;
	size_t cnt = 0;
	for (const auto &rex: chosenTask->GetCmdRegexes()) {
		if (std::regex_match(currentCommand, matches, rex)) break;
		cnt++;
	}
	if (cnt == chosenTask->GetCmdRegexes().size()) {  // didn't match after all??
		taskIter++;
		goto continueSearch;
	}
	
}

bool Game::AttemptMatchSystemCommand() {
	return false;
}

}  // namespace Starlane