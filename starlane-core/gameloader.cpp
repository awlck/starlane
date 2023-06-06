#include "game.h"

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
#include "gamecontent/userfunc.h"
#include "gamecontent/variable.h"
#include "gamecontent/task.h"

namespace Starlane {

Game *Game::LoadFromXML(const std::string &gameTxt) {
	// If a game is already ongoing, delete it.
	// Assume that, if we get here, the user has already consented to this.
	if (Game::theGame)
		delete Game::theGame;

	pugi::xml_document doc;
	auto parseResult = doc.load_string(gameTxt.c_str());
	if (parseResult.status != pugi::status_ok) {
		frontend->FatalError((std::string("Unable to load game: ") + parseResult.description()).c_str());
		return nullptr;
	}
	auto gameNode = doc.child("Adventure");

	auto result = new Game;
	result->gameTitle = gameNode.child_value("Title");
	result->gameAuthor = gameNode.child_value("Author");
	result->gameAdriftVersion = gameNode.child_value("Version");
	result->gameStatusLine = gameNode.child_value("UserStatus");
	result->showFirstLocation = ParseBool(gameNode.child("ShowFirstLocation").child_value());
	result->showExits = ParseBool(gameNode.child("ShowExits").child_value());
	result->gameIntro = result->CreateDescFromXML(gameNode.child("Introduction"));
	Game::theGame = result;

	// It is important that all properties are created before anything tries to use them.
	for (const auto &it: gameNode.children("Property"))
		result->CreatePropertyFromXML(it);
	// Similarly, variables must be known before considering restrictions and task actions.
	for (const auto &it : gameNode.children("Variable"))
		result->CreateVariableFromXML(it);

	for (const auto &it : gameNode.children("Location"))
		result->CreateObjFromXML(it);
	for (const auto &it : gameNode.children("Character"))
		result->CreateObjFromXML(it);
	for (const auto &it: gameNode.children("Object"))
		result->CreateObjFromXML(it);

	for (const auto &it: gameNode.children("Task"))
		result->CreateTaskFromXML(it);

	for (const auto &it : gameNode.children("Event"))
		result->CreateEventFromXML(it);

	for (const auto &it : gameNode.children("Group"))
		result->CreateGroupFromXML(it);

	for (const auto &it: gameNode.children("Function"))
		result->CreateFunctionFromXML(it);

	// Load file path --> blorb resource id mappings, if any.
	const auto &mappingsNode = gameNode.child("FileMappings");
	if (mappingsNode.type() != pugi::node_null) {
		for (const auto &it: mappingsNode.children()) {
			result->blorbResMap[it.child_value("File")] = ParseInt(it.child_value("Resource"));
		}
	}

    result->StartupSanityCheck();

	// load the player character referral person, which for some reason is stored on the `Player` object...
	// (defaults to second person)
	auto perspectiveNode = doc.select_node(R"(//Character[Key="Player"]/Perspective)");
	if (perspectiveNode.node().type() != pugi::node_null)
		result->pcReferralPerson = ParseReferralPerson(perspectiveNode.node().child_value());

	// Finally, pre-split all descriptions into runs of plain text and expressions.
	// (This needs to happen after objects are loaded since we need to determine whether
	//  'A.B' is indeed accessing property 'B' of object with key 'A' (if 'A' is a valid object key)
	//  or just a period not followed by a space (if 'A' is not a valid object key).)
	for (auto &it : result->descriptions)
		it.second->ResolveText();

    return result;
}

size_t Game::CreateDescFromXML(const pugi::xml_node &descNode) {
	descriptions[++descriptionsSoFar] = Description::CreateFromXML(descNode);
	return descriptionsSoFar;
}

void Game::CreateObjFromXML(const pugi::xml_node &objNode) {
	auto result = GameObj::CreateFromXML(objNode);
	objects[result->Key()] = result;
}

void Game::CreatePropertyFromXML(const pugi::xml_node &propNode) {
	auto result = Property::CreateFromXML(propNode);
	properties[result->Key()] = result;
}

size_t Game::CreateRestrictionsFromXML(const pugi::xml_node &restrNode) {
	restrictions[++restrictionsSoFar] = Restriction::CreateFromXML(restrNode);
	return restrictionsSoFar;
}

void Game::CreateTaskFromXML(const pugi::xml_node &propNode) {
	auto result = Task::CreateFromXML(this, propNode);
	tasks[result->Key()] = result;
}

void Game::CreateEventFromXML(const pugi::xml_node &evtNode) {
	auto result = Event::CreateFromXML(evtNode);
	events[result->Key()] = result;
}

void Game::CreateVariableFromXML(const pugi::xml_node &varNode) {
	auto result = Variable::CreateFromXML(varNode);
	variables[result->Key()] = result;
	varNames[result->Name()] = result->Key();
}

void Game::CreateGroupFromXML(const pugi::xml_node &grpNode) {
    auto result = Group::CreateFromXML(grpNode);
    groups[result->Key()] = result;
}

void Game::CreateFunctionFromXML(const pugi::xml_node &funcNode) {
	auto result = UserFunction::CreateFromXML(funcNode);
	userFunctions[result->Key()] = result;
	userFuncNames[result->Name()] = result->Key();
}

// Parts of the program use the top bit to differentiate between plain text snippet references
// and expression references, so we must make sure that we never allocate a plain text
// reference so large that it uses the top bit.
static void CheckSnippetsInRange(size_t snips) {
	// TODO: figure out why clang seems to think that this expression is always false.
	if (snips > (((size_t) 1) << (std::numeric_limits<size_t>::digits - 1))) {
		std::stringstream s;
		s << "My brain just exploded: On your system, I can only handle "
			<< (((size_t) 1) << (std::numeric_limits<size_t>::digits - 1))
			<< " individual runs of plain text, but this game requires more than that.";
		frontend->FatalError(s.str().c_str());
		throw std::out_of_range(s.str());
	}
}

PlainTextRef Game::StorePlainTextSnippet(const std::string &snip) {
	char *c = new char[snip.length() + 1];
	strcpy(c, snip.c_str());
	plainTextSnippets.emplace(++textSnippetsSoFar, c);
	CheckSnippetsInRange(textSnippetsSoFar);
	return textSnippetsSoFar;
}
PlainTextRef Game::StorePlainTextSnippet(std::string_view snip) {
	char *c = new char[snip.length() + 1];
	strncpy(c, snip.data(), snip.length());
	c[snip.length()] = 0;
	plainTextSnippets.emplace(++textSnippetsSoFar, c);
	CheckSnippetsInRange(textSnippetsSoFar);
	return textSnippetsSoFar;
}

ExprRef Game::CreateExpression(const std::string &expr) {
	auto x = knownExprs[expr];
	if (x != 0) return x;

	x = ++expressionsSoFar;
	auto y = new Expression(expr);
	expressions.emplace(x, y);
	knownExprs[expr] = x;
	return x;
}

void Game::StartupSanityCheck() const {
    size_t sanityCheck = std::distance(descriptions.begin(), descriptions.end());
    if (sanityCheck != descriptionsSoFar || sanityCheck != descriptions.size() || descriptionsSoFar != descriptions.size())
        frontend->FatalError("Startup sanity check failed: description count mismatch.");
    sanityCheck = std::distance(restrictions.begin(), restrictions.end());
    if (sanityCheck != restrictionsSoFar || sanityCheck != restrictions.size() || restrictionsSoFar != restrictions.size())
        frontend->FatalError("Startup sanity check failed: restriction count mismatch.");
}

}  // namespace Starlane