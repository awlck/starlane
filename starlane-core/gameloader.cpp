#include "game.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include <pugixml.hpp>

#include "starlane-core.h"
#include "expression.h"
#include "valueparsers.h"
#include "gamecontent/description.h"
#include "gamecontent/event.h"
#include "gamecontent/gameobj.h"
#include "gamecontent/group.h"
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

Game *Game::LoadFromXML(const std::string &gameTxt) {
	// If a game is already ongoing, delete it.
	// Assume that, if we get here, the user has already consented to this.
	// TODO: deal with `startupState` and the undo history.
	if (Game::theGame)
		delete Game::theGame;

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
	// Before parsing the intro (or anything else): an inline conditional segment inside the intro
	// text carries its own <Restrictions>, and Description::Segment::CreateFromXML reaches for
	// Game::Get() to store them -- which is still null until this assignment. (Jacaranda Jim has
	// exactly such a segment in its intro; most games don't, which is why this stayed latent.)
	Game::theGame = result;
	rStatic->gameIntro = result->CreateDescFromXML(gameNode.child("Introduction"));

	{
		pugi::xml_node n;
		if ((n = gameNode.child("TaskExecution")).type() != pugi::node_null &&
				STREQ(n.child_value(), "HighestPriorityPassingTask"))
			rStatic->executionPolicy = ExecutionPolicy::HighestPrioPassing;
		// TODO: games with versions <5.0.0.22 need to use the other one by default.
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

	LOAD_STAGE("Loading Events");
	for (const auto &it : gameNode.children("Event"))
		result->CreateEventFromXML(it);
	
	LOAD_STAGE("Loading Groups");
	for (const auto &it : gameNode.children("Group"))
		result->CreateGroupFromXML(it);

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

	// determine who is the initial player character (can this even be changed?).
	// set by the special element 'Type' on a Character entity.
	auto playerNode = doc.select_node(R"(//Character[Type="Player"]/Key)");
	if (playerNode.node().type() != pugi::node_null)
		result->playerKey = playerNode.node().child_value();
	else  // fallback
		result->playerKey = "Player";


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
	s->varNames[result->Name()] = result->Key();
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