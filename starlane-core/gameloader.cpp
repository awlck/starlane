#include "game.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <limits>

#include <pugixml.hpp>

#include "starlane-core.h"
#include "expression.h"
#include "valueparsers.h"
#include "gamecontent/character.h"
#include "gamecontent/description.h"
#include "gamecontent/event.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
#include "gamecontent/location.h"
#include "gamecontent/property.h"
#include "gamecontent/restriction.h"
#include "gamecontent/synonym.h"
#include "gamecontent/textoverride.h"
#include "gamecontent/userfunc.h"
#include "gamecontent/variable.h"
#include "gamecontent/task.h"

#include <cassert>

#ifndef NDEBUG
#include <iostream>
#define LOAD_STAGE(msg) std::cout << "\n\n==========================================\n" msg "\n==========================================\n"
#else
#define LOAD_STAGE(msg)
#endif


namespace Starlane {

Game *Game::LoadFromXML(const std::string &gameTxt, uint32_t gameCrc32) {
	// If a game is already ongoing, delete it.
	// Assume that, if we get here, the user has already consented to this.
	// Discard() (rather than a bare `delete`) also clears the undo history and the startupState
	// snapshot -- both static and specific to the game being replaced, so left alone they would
	// either dangle or quietly leak into the newly loaded game.
	if (Game::theGame)
		Game::Discard();

	LOAD_STAGE("Parsing File");
	pugi::xml_document doc;
	auto parseResult = doc.load_string(gameTxt.c_str());
	if (parseResult.status != pugi::status_ok) {
		frontend->FatalError((std::string("Unable to load game: ") + parseResult.description()).c_str());
		return nullptr;
	}
	auto gameNode = doc.child("Adventure");

	auto result = new Game;
	auto rStatic = new GameStatic;
	result->staticData = rStatic;
	rStatic->gameCrc32 = gameCrc32;
	rStatic->gameTitle = gameNode.child_value("Title");
	rStatic->gameAuthor = gameNode.child_value("Author");
	rStatic->gameLastUpdated = gameNode.child_value("LastUpdated");
	rStatic->gameAdriftVersion = gameNode.child_value("Version");
	rStatic->gameStatusLine = gameNode.child_value("UserStatus");
	rStatic->showFirstLocation = ParseBool(gameNode.child("ShowFirstLocation").child_value());
	rStatic->showExits = ParseBool(gameNode.child("ShowExits").child_value());
	// ADRIFT only writes this out when it differs from its default of 3, and clamps it at zero
	// on the way in (a negative wait is meaningless, and would wrap around here).
	if (gameNode.child("WaitTurns").type() != pugi::node_null)
		rStatic->waitTurns = (uint32_t) std::max((int64_t) 0, ParseInt(gameNode.child_value("WaitTurns")));
	{
		// A game can replace ADRIFT's default English direction words wholesale via these 12
		// elements (FileIO.vb), each written only when it differs from the editor's own default at
		// save time -- e.g. <DirectionEast>Clockwise/CW</DirectionEast> to make a circular map's
		// exits read "clockwise"/"cw" instead of "east"/"e" (the displayed/canonical name, "East",
		// is unaffected either way).
		static const std::pair<const char *, const char *> kDirectionElements[] = {
			{ "North", "DirectionNorth" }, { "NorthEast", "DirectionNorthEast" },
			{ "East", "DirectionEast" }, { "SouthEast", "DirectionSouthEast" },
			{ "South", "DirectionSouth" }, { "SouthWest", "DirectionSouthWest" },
			{ "West", "DirectionWest" }, { "NorthWest", "DirectionNorthWest" },
			{ "In", "DirectionIn" }, { "Out", "DirectionOut" },
			{ "Up", "DirectionUp" }, { "Down", "DirectionDown" },
		};
		std::unordered_map<std::string, std::string> overrides;
		for (const auto &[canonical, element] : kDirectionElements) {
			auto n = gameNode.child(element);
			if (n.type() != pugi::node_null && *n.child_value())
				overrides[canonical] = n.child_value();
		}
		rStatic->directionTable = Util::BuildDirectionTable(overrides);
	}
	if (gameNode.child("UserStatus").type() != pugi::node_null)
		rStatic->userStatusBar = result->CreateDescFromText(gameNode.child_value("UserStatus"));
	// Before parsing the intro (or anything else): an inline conditional segment inside the intro
	// text carries its own <Restrictions>, and Description::Segment::CreateFromXML reaches for
	// Game::Get() to store them -- which is still null until this assignment. (Jacaranda Jim has
	// exactly such a segment in its intro; most games don't, which is why this stayed latent.)
	Game::theGame = result;

	// Determine who is the initial player character (can this even be changed?), set by the special
	// element 'Type' on a Character entity. Done up front, before objects load, because an object or
	// character that starts out held by the player stores its container as the reference "%Player%"
	// and needs the player key to resolve it (see GameObj/Character CreateFromXML).
	auto playerNode = doc.select_node(R"(//Character[Type="Player"]/Key)");
	if (playerNode.node().type() != pugi::node_null)
		result->playerKey = playerNode.node().child_value();
	else  // fallback
		result->playerKey = "Player";

	rStatic->gameIntro = result->CreateDescFromXML(gameNode.child("Introduction"));

	{
		pugi::xml_node n;
		if ((n = gameNode.child("TaskExecution")).type() != pugi::node_null &&
				STREQ(n.child_value(), "HighestPriorityPassingTask"))
			rStatic->executionPolicy = ExecutionPolicy::HighestPrioPassing;
		else if (n.type() == pugi::node_null) {
			// ADRIFT only ever wrote out <TaskExecution> when it differed from the editor's own
			// default at the time (mirroring how WaitTurns is handled above) -- so a game built
			// before that default changed can be missing the element even though it was authored
			// (and tested) against HighestPriorityPassingTask. The stock Runner doesn't correct for
			// this: it just applies its current default (HighestPriorityTask) regardless of the
			// game's vintage, which is a known bug that leaves at least a couple of real ADRIFT
			// games unwinnable. We deliberately don't reproduce it: for a file built before ADRIFT
			// 5.0.0.22 (encoded as the double 5.000022 in <Version>, e.g. "5.000021") with no
			// explicit <TaskExecution>, default to HighestPriorityPassingTask instead.
			double ver = std::strtod(rStatic->gameAdriftVersion.c_str(), nullptr);
			if (ver > 0.0 && ver < 5.000022)
				rStatic->executionPolicy = ExecutionPolicy::HighestPrioPassing;
		}
	}

	LOAD_STAGE("Loading Properties");
	// It is important that all properties are created before anything tries to use them.
	for (const auto &it: gameNode.children("Property"))
		result->CreatePropertyFromXML(it);

	LOAD_STAGE("Loading Variables");
	// Similarly, variables must be known before considering restrictions and task actions.
	for (const auto &it : gameNode.children("Variable"))
		result->CreateVariableFromXML(it);

	LOAD_STAGE("Loading Objects");
	for (const auto &it : gameNode.children("Location"))
		result->CreateObjFromXML(it);
	for (const auto &it : gameNode.children("Character"))
		result->CreateObjFromXML(it);
	for (const auto &it: gameNode.children("Object"))
		result->CreateObjFromXML(it);

	// A Player left "Hidden" (with no location of its own) begins in the first-defined location,
	// as the ADRIFT Runner does -- otherwise the player would start nowhere, seeing nothing and
	// unable to act on anything. Done here, once, so every later snapshot (startup state, undo
	// history) inherits the placement rather than each having to rediscover it.
	if (auto *player = dynamic_cast<Character *>(result->TryGetObject(result->playerKey));
	    player && player->GetLocationKey().empty()) {
		for (const auto &key : rStatic->objectLoadOrder) {
			if (dynamic_cast<Location *>(result->TryGetObject(key))) {
				player->SetInitialLocation(key);
				break;
			}
		}
	}

	LOAD_STAGE("Loading Groups");
	// Groups only need the objects/locations/characters and properties loaded above -- moved
	// ahead of Tasks so that a task action's parameter parsing (e.g. Task::Action::CreateFromXML
	// recognizing "Npcs.Gender" as naming the "Npcs" group) can already see which keys name a
	// group rather than an object.
	for (const auto &it : gameNode.children("Group"))
		result->CreateGroupFromXML(it);

	LOAD_STAGE("Loading Tasks");
	for (const auto &it: gameNode.children("Task")) {
		auto t = result->CreateTaskFromXML(it);
		rStatic->prioOrderedTasks.insert(t);
	}
	// Index Specific tasks by the General task they override, and System tasks by whatever runs
	// them of its own accord. All in priority order, which prioOrderedTasks already iterates in,
	// so each list comes out sorted for free.
	for (Task *t : rStatic->prioOrderedTasks) {
		if (t->GetType() == Task::Type::Specific && !t->OverridesTask().empty())
			rStatic->specificChildren[t->OverridesTask()].push_back(t);
		if (t->GetType() == Task::Type::System) {
			if (!t->LocationTrigger().empty())
				rStatic->systemTasksByLocation[t->LocationTrigger()].push_back(t);
			if (t->RunsImmediately())
				rStatic->runImmediatelyTasks.push_back(t);
		}
	}

	// Character walks name the tasks that start and stop them; those tasks have just finished
	// loading, so wire each walk up to them now. The characters themselves loaded earlier, before any
	// task existed, which is why this can't happen while a character is being built.
	for (const auto &key : rStatic->objectLoadOrder)
		if (auto *c = dynamic_cast<Character *>(result->TryGetObject(key)))
			c->RegisterWalkNotifications();

	LOAD_STAGE("Loading Events");
	for (const auto &it : gameNode.children("Event"))
		result->CreateEventFromXML(it);

	LOAD_STAGE("Loading Functions");
	for (const auto &it: gameNode.children("Function"))
		result->CreateFunctionFromXML(it);

	LOAD_STAGE("Loading Synonyms");
	for (const auto &it: gameNode.children("Synonym"))
		result->CreateSynonymFromXML(it);

	LOAD_STAGE("Loading Text Overrides");
	for (const auto &it: gameNode.children("TextOverride"))
		result->CreateTextOverrideFromXML(it);

	// Load file path --> blorb resource id mappings, if any.
	const auto &mappingsNode = gameNode.child("FileMappings");
	if (mappingsNode.type() != pugi::node_null) {
		for (const auto &it: mappingsNode.children()) {
			rStatic->blorbResMap[it.child_value("File")] = ParseInt(it.child_value("Resource"));
		}
	}

    result->StartupSanityCheck();

	// load the player character referral person, which for some reason is stored on the `Player` object...
	// (defaults to second person)
	auto perspectiveNode = doc.select_node(R"(//Character[Key="Player"]/Perspective)");
	if (perspectiveNode.node().type() != pugi::node_null)
		rStatic->pcReferralPerson = ParseReferralPerson(perspectiveNode.node().child_value());

	LOAD_STAGE("Figuring Out Text");
	// Finally, pre-split all descriptions into runs of plain text and expressions.
	// (This needs to happen after objects are loaded since we need to determine whether
	//  'A.B' is indeed accessing property 'B' of object with key 'A' (if 'A' is a valid object key)
	//  or just a period not followed by a space (if 'A' is not a valid object key).)
#ifndef NDEBUG
	size_t count = 0;
	for (auto &it : result->descriptions) {
		if (++count % 250 == 0)
			std::cout << count << "... ";
		it.second->ResolveText();
	}
#else
	for (auto &it : result->descriptions)
		it.second->ResolveText();
#endif

	LOAD_STAGE("Done!");

    return result;
}

size_t Game::CreateDescFromXML(const pugi::xml_node &descNode) {
	descriptions[++descriptionsSoFar] = Description::CreateFromXML(descNode);
	return descriptionsSoFar;
}

size_t Game::CreateDescFromText(const std::string &text) {
	descriptions[++descriptionsSoFar] = Description::CreateFromText(text);
	return descriptionsSoFar;
}

void Game::CreateObjFromXML(const pugi::xml_node &objNode) {
	auto result = GameObj::CreateFromXML(objNode);
	assert(result);
	objects[result->Key()] = result;
	const_cast<GameStatic *>(staticData)->objectLoadOrder.push_back(result->Key());
}

void Game::CreatePropertyFromXML(const pugi::xml_node &propNode) {
	auto result = Property::CreateFromXML(propNode);
	assert(result);
	auto s = const_cast<GameStatic *>(staticData);
	s->properties[result->Key()] = result;
}

size_t Game::CreateRestrictionsFromXML(const pugi::xml_node &restrNode) {
	auto s = const_cast<GameStatic *>(staticData);
	s->restrictions[++restrictionsSoFar] = Restriction::CreateFromXML(restrNode);
	return restrictionsSoFar;
}

Task *Game::CreateTaskFromXML(const pugi::xml_node &propNode) {
	auto result = Task::CreateFromXML(this, propNode);
	assert(result);
	auto s = const_cast<GameStatic *>(staticData);
	s->tasks[result->Key()] = result;
	return result;
}

void Game::CreateEventFromXML(const pugi::xml_node &evtNode) {
	auto result = Event::CreateFromXML(evtNode);
	assert(result);
	events[result->Key()] = result;
	const_cast<GameStatic *>(staticData)->eventLoadOrder.push_back(result->Key());
}

void Game::CreateVariableFromXML(const pugi::xml_node &varNode) {
	auto result = Variable::CreateFromXML(varNode);
	assert(result);
	variables[result->Key()] = result;
	auto s = const_cast<GameStatic *>(staticData);
	// Keyed by the lowercased name: ADRIFT matches a %name% reference against a variable's name
	// case-insensitively (ReplaceFunctions uses CompareMethod.Text), and games rely on it -- one
	// variable is named "seabonus" but referenced as "%Seabonus%".
	s->varNames[Util::ToLower(result->Name())] = result->Key();
}

void Game::CreateGroupFromXML(const pugi::xml_node &grpNode) {
    auto result = Group::CreateFromXML(grpNode);
	assert(result);
    groups[result->Key()] = result;
}

void Game::CreateFunctionFromXML(const pugi::xml_node &funcNode) {
	auto result = UserFunction::CreateFromXML(funcNode);
	assert(result);
	auto s = const_cast<GameStatic *>(staticData);
	s->userFunctions[result->Key()] = result;
	s->userFuncNames[result->Name()] = result->Key();
}

void Game::CreateSynonymFromXML(const pugi::xml_node &synoNode) {
	auto result = Synonym::CreateFromXML(synoNode);
	assert(result);
	auto s = const_cast<GameStatic *>(staticData);
	s->synonyms[result->Key()] = result;
}

void Game::CreateTextOverrideFromXML(const pugi::xml_node &toNode) {
	auto result = TextOverride::CreateFromXML(this, toNode);
	assert(result);
	auto s = const_cast<GameStatic *>(staticData);
	s->textOverrides[result->Key()] = result;
}

// Plain text and expressions are miscible in some parts of the program (particularly,
// within the contents of descriptions), so in order to tell them apart, plain text snipped IDs
// are always positive while expression IDs are negative. Make sure the plain text snippet count
// doesn't overflow. (The type used to store them is architecture-dependent for performance reasons --
// 32 bits on 32-bit systems, 64 bits on 64-bit systems. So this is exceedingly unlikely to ever
// trigger on a 64-bit system, however there is a remote chance that some overly-ambitious game
// might exceed the limit of ~2.147 billion runs of plain text supported on a 32-bit system. But,
// then again, it seems likely that we would run out of memory and/or address space to store all
// this text first. Oh well.)
static void CheckSnippetsInRange(ptrdiff_t snips) {
	if (snips == (std::numeric_limits<ptrdiff_t>::max() - 1)) {
		std::string s("My brain just exploded: On your system, I can only handle ");
		s += std::to_string(std::numeric_limits<ptrdiff_t>::max() - 1);
		s += " individual runs of plain text, but this game requires more than that.";
		frontend->FatalError(s.c_str());
		throw std::out_of_range(s);
	}
}

PlainTextRef Game::StorePlainTextSnippet(const std::string &snip) {
	char *c = new char[snip.length() + 1];
	strcpy(c, snip.c_str());
	auto s = const_cast<GameStatic *>(staticData);
	s->plainTextSnippets.emplace(++textSnippetsSoFar, c);
	CheckSnippetsInRange(textSnippetsSoFar);
	return textSnippetsSoFar;
}
PlainTextRef Game::StorePlainTextSnippet(std::string_view snip) {
	char *c = new char[snip.length() + 1];
	strncpy(c, snip.data(), snip.length());
	c[snip.length()] = 0;
	auto s = const_cast<GameStatic *>(staticData);
	s->plainTextSnippets.emplace(++textSnippetsSoFar, c);
	CheckSnippetsInRange(textSnippetsSoFar);
	return textSnippetsSoFar;
}

ExprRef Game::CreateExpression(const std::string &expr) {
	auto x = knownExprs[expr];
	if (x != 0) return x;

	x = -(++expressionsSoFar);
	auto result = new Expression(expr);
	auto s = const_cast<GameStatic *>(staticData);
	s->expressions.emplace(x, result);
	knownExprs[expr] = x;
	return x;
}

void Game::StartupSanityCheck() const {
    size_t sanityCheck = std::distance(descriptions.begin(), descriptions.end());
    if (sanityCheck != descriptionsSoFar || sanityCheck != descriptions.size() || descriptionsSoFar != descriptions.size())
        frontend->FatalError("Startup sanity check failed: description count mismatch.");
    sanityCheck = std::distance(staticData->restrictions.begin(), staticData->restrictions.end());
    if (sanityCheck != restrictionsSoFar || sanityCheck != staticData->restrictions.size() || restrictionsSoFar != staticData->restrictions.size())
        frontend->FatalError("Startup sanity check failed: restriction count mismatch.");
}

}  // namespace Starlane