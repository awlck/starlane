//
// Created by Adrian Welcker on 11.07.23.
//

#include "game.h"

#include <regex>

#include "gamecontent/description.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/synonym.h"
#include "gamecontent/character.h"

namespace Starlane {

namespace {
enum class ReferenceType {
	Object,
	Character,
	Direction
};
}  // anonymous namespace

std::string Game::ApplySynonyms(std::string s) {
	for (const auto &it: staticData->synonyms) {
		for (const auto &f: it.second->GetFrom()) {
			size_t n;
			while ((n = s.find(f)) != std::string::npos)
				s.replace(n, f.size(), it.second->GetReplacement());
		}
	}
	return s;
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

std::vector<std::string> Game::MatchListForReference(const std::string &from, const std::string &refType) const {
	using namespace std::string_literals;
	ReferenceType rt;
	if (refType.substr(0, sizeof("object")-1) == "object"s) rt = ReferenceType::Object;
	else if (refType.substr(0, sizeof("character")-1) == "character"s) rt = ReferenceType::Character;
	else if (refType.substr(0, sizeof("direction")-1) == "direction"s) rt = ReferenceType::Direction;
	else throw std::runtime_error("Unknown reference type in task: " + refType);

	std::vector<std::string> result;
	for (const auto &it : objects) {
		switch (rt) {
			case ReferenceType::Object:
				if (dynamic_cast<Character *>(it.second)) continue;
				break;
			case ReferenceType::Character:
				if (!dynamic_cast<Character *>(it.second)) continue;
				break;
			case ReferenceType::Direction:
				return {};  // TODO: account for directions
		}
		if (std::regex_match(from, it.second->GetMatchExpr()))
			result.push_back(it.first);
	}
	return {};
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
		OutputFiltered("I didn't understand that sentence.\n");
		return;
	} else {
		chosenTask = *taskIter;
	}

	// output failure message if restrictions failed
	if (!eligible.first) {
		if (eligible.second != 0) {
			OutputFiltered(GetDescription(eligible.second)->Build());
		} else {
			OutputFiltered("You can't do that right now.\n");
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
	currentRefs.clear();
	{
		const auto &refSpecs = chosenTask->GetGroupCoding()[cnt];
		std::vector<std::string> currentMatchList;
		for (size_t i = 0; i < refSpecs.size(); i++) {
			const std::string &ref = refSpecs[i];
			if (ref == "text") {
				currentRefs[ref] = matches[i+1];
				continue;
			}
			currentMatchList = std::move(MatchListForReference(matches[i + 1], ref));
			// TODO: reduce match list, etc.
		}
	}
}

bool Game::AttemptMatchSystemCommand() {
	return false;
}

}  // namespace Starlane