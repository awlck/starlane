#pragma once

#ifndef SLC_GAME_H
#define SLC_GAME_H

#include <memory>
#include <string>
#include <unordered_map>

namespace Starlane {
class Description;
class Restriction;
class GameObj;

class Game {
public:
	static Game *LoadFromXML(const std::string &gameTxt);
private:
	Game() {};

	std::unordered_map<std::string, GameObj *> objects;
	std::string gameTitle;
	std::string gameAuthor;
	std::string gameAdriftVersion;
};

}

#endif  // !SLC_GAME_H