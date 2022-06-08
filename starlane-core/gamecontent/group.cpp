#include "group.h"

#include <pugixml.hpp>

#include "../game.h"
#include "gameobj.h"

namespace Starlane {

Group *Group::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Group;
	result->key = xmlNode.child_value("Key");
	result->name = xmlNode.child_value("Name");
	// Objects and locations have already been created when this function is called,
	// so doing this is OK:
	for (const auto &it : xmlNode.children("Member"))
		result->AddObj(it.child_value());
	for (const auto &it : xmlNode.children("Property"))
		result->SetPropValueFromXML(it);
	return result;
}

void Group::AddObj(const std::string &key) {
	Game::Get()->GetObject(key)->BecomeGroupMember(this->key);
	ReceiveObj(key);
}
void Group::AddObj(GameObj *obj) {
	obj->BecomeGroupMember(this->key);
	ReceiveObj(obj->Key());
}

void Group::RemoveObj(const std::string &key) {
	Game::Get()->GetObject(key)->CeaseBeingGroupMember(this->key);
	LetGoOfObj(key);
}
void Group::RemoveObj(GameObj *obj) {
	obj->CeaseBeingGroupMember(this->key);
	LetGoOfObj(obj->Key());
}

}