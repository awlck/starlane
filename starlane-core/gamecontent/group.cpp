#include "group.h"

#include <pugixml.hpp>

#include "../game.h"
#include "gameobj.h"
#include "../savefiles/writer.h"

namespace Starlane {

Group *Group::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Group;
	result->key_ = xmlNode.child_value("Key");
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
	// A group that lists a member key naming no object is malformed; skip it rather than
	// dereferencing a missing object.
	GameObj *obj = Game::Get()->MutableObject(key);
	if (!obj) {
		LogError("Group '" + key_ + "' names nonexistent member '" + key + "'; skipping.");
		return;
	}
	obj->BecomeGroupMember(this->key_);
	ReceiveObj(key);
}
void Group::AddObj(GameObj *obj) {
	obj->BecomeGroupMember(this->key_);
	ReceiveObj(obj->Key());
}

void Group::RemoveObj(const std::string &key) {
	if (GameObj *obj = Game::Get()->MutableObject(key))
		obj->CeaseBeingGroupMember(this->key_);
	LetGoOfObj(key);
}
void Group::RemoveObj(GameObj *obj) {
	obj->CeaseBeingGroupMember(this->key_);
	LetGoOfObj(obj->Key());
}

void Group::WriteState(Save::Writer &writer) const {
	// hmm... do we even need groups to know their members post-load? I don't think so.
	writer.WriteSortedMap(GetAllIntProps());
	writer.WriteSortedMap(GetAllStrProps());
}

bool Group::RestoreState(const Save::AstNode *node) {
	ClearProps();
	ITERATE_CHILDREN(node, prop) {
		if (prop->type == Save::NT_INT)
			SetPropValue(prop->myName, prop->sv.Int);
		else
			SetPropValue(prop->myName, prop->Str);
	}
	return true;
}

}