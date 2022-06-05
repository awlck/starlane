#pragma once

#ifndef SLC_GAME_H
#define SLC_GAME_H

#include "slc_private.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace pugi {
class xml_node;
}

namespace Starlane {

class Game {
public:
	static Game *LoadFromXML(const std::string &gameTxt);
	DescrRef CreateDescFromXML(const pugi::xml_node &descNode);
	RestrRef CreateRestrictionsFromXML(const pugi::xml_node &restrNode);
	void CreatePropertyFromXML(const pugi::xml_node &propNode);

private:
	Game() = default;

	std::unordered_map<std::string, GameObj *> objects;
	std::unordered_map<DescrRef, Description *> descriptions;
	std::unordered_map<RestrRef, Restriction *> restrictions;
	std::unordered_map<std::string, Property *> properties;

	std::string gameTitle;
	std::string gameAuthor;
	std::string gameAdriftVersion;
	std::string gameStatusLine;
	bool showFirstLocation;
	bool showExits;
	DescrRef gameIntro = 0;

	size_t descriptionsSoFar = 0;
	size_t restrictionsSoFar = 0;
};

}

#endif  // !SLC_GAME_H