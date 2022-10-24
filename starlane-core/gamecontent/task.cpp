#include "task.h"

#include <algorithm>
#include <sstream>

#include <pugixml.hpp>

#include "../game.h"
#include "../expression.h"
#include "../valueparsers.h"
#include "character.h"
#include "event.h"
#include "group.h"
#include "location.h"
#include "property.h"
#include "restriction.h"
#include "variable.h"

namespace Starlane {

namespace {
bool MaybeIsExpr(const std::string &s) {
	size_t pos = 0;
	for (; pos < s.length(); ++pos) {
		auto x = s[pos];
		if (x == '%' || x == '"' || x == '.') return true;
		if (x == ' ') return false;
	}
	return false;
}
}

Task *Task::CreateFromXML(Game *g, const pugi::xml_node &xmlNode) {
	auto result = new Task;
	result->key = xmlNode.child_value("Key");
	result->priority = ParseInt(xmlNode.child_value("Priority"));
	result->type = Task::ParseType(xmlNode.child_value("Type"));
	result->command = xmlNode.child_value("Command");
	result->descr = xmlNode.child_value("Description");
	result->completionMsg = Game::Get()->CreateDescFromXML(xmlNode.child("CompletionMessage"));
	result->repeatable = ParseBool(xmlNode.child_value("Repeatable"));
	result->restrictions = g->CreateRestrictionsFromXML(xmlNode.child("Restrictions"));
	{
		const auto &foNode = xmlNode.child("FailOverride");
		if (foNode.type() != pugi::node_null)
			result->overrideFailMsg = Game::Get()->CreateDescFromXML(foNode);
	}
	if (result->type == Type::Specific) {
		for (const auto &spcNode: xmlNode.children("Specific")) {
			SpecificInfo spi;
			spi.type = STREQ(spcNode.child_value("Type"), "Text") ? SpType::Text : SpType::Object;
			spi.multiple = ParseBool(spcNode.child_value("Multiple"));
			if (spi.type != SpType::Text) {
				spi.key = spcNode.child_value("Key");
			}
			if (std::count_if(spcNode.begin(), spcNode.end(), [&](const auto &item) {
				return STREQ(item.name(), "Key");
			}) > 1) throw std::runtime_error(std::string("In task ") + result->key + ": specific tasks with multiple explicitly-named objects in the same reference are currently unsupported.");
		}
		result->overrideType = ParseOverrideType(xmlNode.child_value("SpecificOverrideType"));
	}

	for (const auto &it: xmlNode.child("Actions").children())
		result->actions.push_back(Action::CreateFromXML(it));

	return result;
}

Task::Action Task::Action::CreateFromXML(const pugi::xml_node &xmlNode) {
	Action result;
	std::string name = xmlNode.name();
	std::istringstream strm(std::string(xmlNode.child_value()));
	std::string t;
	std::vector<std::string> tokens;
	while (std::getline(strm, t, ' '))
		tokens.push_back(t);

	// Deal with, e.g., "move every object with property x set to y to location z"
	if (tokens[0] == "EverythingWithProperty" || tokens[0] == "EveryoneWithProperty") {
		// special cases. love it.
		result.prop = tokens[1];
		std::string temp;
		for (size_t idx = 2; idx < tokens.size()-2; idx++) {
			if (idx != 2)
				temp += ' ';
			temp += tokens[idx];
		}
		// make an expression if necessary
		switch (Game::Get()->GetPropMeta(result.prop)->Type()) {
		case Property::ValueType::Object:
		case Property::ValueType::Enum:
		case Property::ValueType::Map:
			result.lhs = temp;
			break;
		case Property::ValueType::Bool:
			// this means that the property must be "set" (i.e., true)
			break;
		default:
			result.expr = Game::Get()->CreateExpression(temp);
		}
		// hackily remove the stuff we've handled so the rest of the function doesn't throw up
		tokens.erase(tokens.begin() + 2, tokens.end() - 2);
	}

	if (name == "MoveObject" || name == "MoveCharacter") {
		result.lhs = tokens[1];
		if (tokens[2] == "ToParentLocation") {
			result.type = ActionType::MoveToParent;
		} else {
			result.rhs = tokens[3];
		}
		if (tokens[2] == "ToLocation") {
			result.type = ActionType::MoveToLocation;
		} else if (tokens[2] == "ToSameLocationAs") {
			result.type = ActionType::MoveToLocationOf;
		} else if (tokens[2] == "ToLocationGroup") {
			result.type = ActionType::MoveToGroup;
		} else if (tokens[2] == "InsideObject") {
			result.type = ActionType::MoveInsideObj;
		} else if (tokens[2] == "OntoObject") {
			result.type = ActionType::MoveOntoObj;
		} else if (tokens[2] == "ToCarriedBy") {
			result.type = ActionType::MakeCarriedBy;
		} else if (tokens[2] == "ToWornBy") {
			result.type = ActionType::MakeWornBy;
		} else if (tokens[2] == "ToPartOfCharacter" || tokens[2] == "ToPartOfObject") {
			result.type = ActionType::MakePartOf;
		} else if (tokens[2] == "ToParentLocation") {
			result.type = ActionType::MoveToParent;
		} else if (tokens[2] == "InDirection") {
			result.type = ActionType::MoveInDirection;
		} else if (tokens[2] == "ToStandingOn") {
			result.type = ActionType::MakeStandingOn;
		} else if (tokens[2] == "ToSittingOn") {
			result.type = ActionType::MakeSittingOn;
		} else if (tokens[2] == "ToLyingOn") {
			result.type = ActionType::MakeLyingOn;
		} else throw std::runtime_error(std::string("Unknown movement command: ") + tokens[2]);
		// reference type determined below.
	} else if (name == "AddObjectToGroup" || name == "AddCharacterToGroup" || name == "AddLocationToGroup") {
		result.type = ActionType::AddToGroup;
		result.lhs = tokens[1];
		result.lhs = tokens[3];
		// reference type determined below.
	} else if (name == "RemoveObjectFromGroup" || name == "RemoveCharacterFromGroup" || name == "RemoveLocationFromGroup") {
		result.type = ActionType::RemoveFromGroup;
		result.lhs = tokens[1];
		result.lhs = tokens[3];
		// reference type determined below.
	} else if (name == "SetProperty") {
		result.type = ActionType::SetPropTo;
		result.lhs = tokens[0];
		result.prop = tokens[1];
		std::string temp;
		for (size_t idx = 2; idx < tokens.size(); idx++) {
			if (idx != 2)
				temp += ' ';
			temp += tokens[idx];
		}
		// make an expression if necessary
		switch (Game::Get()->GetPropMeta(result.prop)->Type()) {
		case Property::ValueType::Object:
		case Property::ValueType::Enum:
		case Property::ValueType::Bool:
			result.rhs = temp;
			break;
		default:
			result.expr = Game::Get()->CreateExpression(temp);
		}
		result.refType = ActionRefType::None;
		return result;
	} else if (name == "SetTasks") {
		if (tokens[0] == "Execute")
			result.type = ActionType::ExecTask;
		else if (tokens[0] == "FOR")
			throw std::runtime_error("Looped task execution is currenly unsupported.");
		else result.type = ActionType::UnsetTask;
		result.refType = ActionRefType::Task;
		result.lhs = tokens[1];
		return result;
	} else if (name == "Time") {
		result.type = ActionType::SkipTurns;
		result.refType = ActionRefType::None;
		result.lhs = tokens[1];
		if (result.lhs[0] == '"') {
			result.lhs = result.lhs.erase(result.lhs.size()-1, 1).erase(0, 1);
		}
		return result;
	} else if (name == "SetVariable" || name == "IncVariable" || name == "DecVariable") {
		if (tokens[0] == "FOR")
			throw std::runtime_error("Looped variable setting is currently unsupported.");
		if (name == "SetVariable") result.type = ActionType::SetVarTo;
		else if (name == "IncVariable") result.type = ActionType::IncVar;
		else result.type = ActionType::DecVar;
		result.refType = ActionRefType::None;
		result.lhs = tokens[0];
		// ignore tokens[1]
		std::string temp(tokens[2]);
		for (size_t idx = 3; idx < tokens.size(); idx++) {
			temp += ' ' + tokens[idx];
		}
		// remove extraneous quotation marks that Adrift adds here
		if (!temp.empty() && temp[0] == '"' && temp[temp.length() - 1] == '"') {
			std::string varnameToSearch;
			size_t l;
			if ((l = tokens[0].find('[')) != std::string::npos) {
				varnameToSearch = tokens[0].substr(0, l);
			} else varnameToSearch = tokens[0];
			Variable::Type vartype = Game::Get()->GetVariable(varnameToSearch)->GetType();
			auto temp2 = temp.substr(1, temp.length() - 2);
			if (vartype != Variable::Type::String || MaybeIsExpr(temp2))
				temp2.swap(temp);
		}
		result.expr = Game::Get()->CreateExpression(temp);
		return result;
	} else if (name == "Conversation") {
		result.refType = ActionRefType::None;
		std::transform(tokens[0].begin(), tokens[0].end(), tokens[0].begin(), [](auto c) { return toupper(c); });
		if (tokens[0] == "GREET" || tokens[0] == "FAREWELL" || tokens[0] == "ASK" || tokens[0] == "TELL") {
			if (tokens[0] == "GREET") result.type = ActionType::ConvoGreet;
			else if (tokens[0] == "FAREWELL") result.type = ActionType::ConvoFarewell;
			else if (tokens[0] == "ASK") result.type = ActionType::ConvoAsk;
			else result.type = ActionType::ConvoTell;
			result.lhs = tokens[1];
			if (tokens.size() >= 3) {
				// ignore tokens[2]
				for (size_t idx = 3; idx < tokens.size(); idx++) {
					result.rhs += (idx == 3 ? "" : " ") + tokens[idx];
				}
			}
		} else if (tokens[0] == "SAY") {
			result.type = ActionType::ConvoCmd;
			for (size_t idx = 1; idx < tokens.size()-3; idx++) {
				result.rhs += (idx == 1 ? "" : " ") + tokens[idx];
			}
			result.lhs = tokens[tokens.size()-1];
		} else if (tokens[0] == "ENTERWITH") {
			result.type = ActionType::ConvoEnter;
			result.lhs = tokens[1];
		} else if (tokens[0] == "LEAVEWITH") {
			result.type = ActionType::ConvoLeave;
			result.lhs = tokens[1];
		}
		return result;
	} else if (name == "EndGame") {
		result.refType = ActionRefType::None;
		if (tokens[0] == "Neutral")
			result.type = ActionType::GameEndNeutral;
		else if (tokens[0] == "Win")
			result.type = ActionType::GameWin;
		else if (tokens[0] == "Lose")
			result.type = ActionType::GameLose;
		else
			result.type = ActionType::GameContinue;
		return result;
	}

	if (tokens[0] == "Object" || tokens[0] == "Character" || tokens[0] == "Location")
		result.refType = ActionRefType::SingleObj;
	else if (tokens[0] == "EverythingHeldBy")
		result.refType = ActionRefType::ObjsHeldBy;
	else if (tokens[0] == "EverythingWornBy")
		result.refType = ActionRefType::ObjsWornBy;
	else if (tokens[0] == "EverythingInside")
		result.refType = ActionRefType::ObjsInside;
	else if (tokens[0] == "EverythingOn")
		result.refType = ActionRefType::ObjsOn;
	else if (tokens[0] == "EverythingWithProperty")
		result.refType = ActionRefType::ObjsWithProp;
	else if (tokens[0] == "EverythingInGroup")
		result.refType = ActionRefType::ObjsInGroup;
	else if (tokens[0] == "EverythingAtLocation")
		result.refType = ActionRefType::ObjsAtLocation;
	else if (tokens[0] == "EveryoneInside")
		result.refType = ActionRefType::CharsInside;
	else if (tokens[0] == "EveryoneOn")
		result.refType = ActionRefType::CharsOn;
	else if (tokens[0] == "EveryoneWithProperty")
		result.refType = ActionRefType::CharsWithProp;
	else if (tokens[0] == "EveryoneInGroup")
		result.refType = ActionRefType::CharsInGroup;
	else if (tokens[0] == "EveryoneAtLocation")
		result.refType = ActionRefType::CharsAtLocation;
	else if (tokens[0] == "LocationOf")
		result.refType = ActionRefType::LocationOf;
	else if (tokens[0] == "EverywhereInGroup")
		result.refType = ActionRefType::LocationsInGroup;
	else if (tokens[0] == "EverywhereWithProperty")
		result.refType = ActionRefType::LocationsWithProp;
	else throw std::runtime_error(std::string("Unknown reference mode: ") + tokens[0]);

	return result;
}

bool Task::Completed() const {
	return Game::Get()->GetIsTaskCompleted(key);
}

std::pair<bool, DescrRef> Task::Eligible() const {
	if (restrictions == 0)  // i.e., no restrictions set
		return { true, 0 };
	return Game::Get()->GetRestriction(restrictions)->PassRestrictionBlock(true);
}

std::pair<bool, DescrRef> Task::Execute() {
	auto elig = Game::Get()->GetRestriction(restrictions)->PassRestrictionBlock();
	if (!elig.first) return elig;
	// TODO
	return { true, completionMsg };
}

void Task::RegisterNotification(const std::string &evtKey, Util::Control::Condition cond) {
	switch (cond) {
		case Util::Control::Condition::Completion:
			completeSubs.emplace_back(evtKey);
			break;
		case Util::Control::Condition::Uncompletion:
			uncompleteSubs.push_back(evtKey);
			break;
	}
}

void Task::Uncomplete() {
	if (Completed()) {
		SendUncompleteNotifications();
		Game::Get()->SetTaskCompleted(key, false);
	}
}

void Task::SendCompleteNotifications() const {
	for (const auto &it: completeSubs) {
		Game::Get()->GetEvent(it)->ReceiveTaskNotification(Util::Control::Condition::Completion, key);
	}
}

void Task::SendUncompleteNotifications() const {
	for (const auto &it: uncompleteSubs) {
		Game::Get()->GetEvent(it)->ReceiveTaskNotification(Util::Control::Condition::Uncompletion, key);
	}
}

void Task::Action::Perform() {
	std::string lhs_ = Util::IsReference(lhs) ? Game::Get()->GetReference(lhs) : lhs;
	std::string rhs_ = Util::IsReference(rhs) ? Game::Get()->GetReference(rhs) : rhs;

	// ugly hack is ugly, but I'm not certain what else to do here
	// still beats modifying this and hoping that we don't forget to undo the modification
	// before returning.
	if (Util::IsList(lhs_)) {
		Task::Action act(*this);
		act.rhs = rhs_;
		// yet another impromptu string splitting implementation, yay me!
		// remind me why again I thought C++ would be a good language for working with lots of text?
		size_t saveidx = 0;
		for (size_t i = 0; i < lhs_.size(); i++) {
			if (lhs_[i] != '|') continue;
			act.lhs = lhs_.substr(saveidx, i - saveidx - 1);
			act.Perform();
		}
	} else if (Util::IsList(rhs_)) {
		Task::Action act(*this);
		act.lhs = lhs_;
		size_t saveidx = 0;
		for (size_t i = 0; i < lhs_.size(); i++) {
			if (rhs_[i] != '|') continue;
			act.rhs = rhs_.substr(saveidx, i - saveidx - 1);
			act.Perform();
		}
	} else if (lhs != lhs_ || rhs != rhs_) {
		Task::Action act(*this);
		act.lhs = lhs_;
		act.rhs = rhs_;
		act.PerformImpl();
	} else return PerformImpl();
}

// discount lookup tables
static inline constexpr GameObj::HoldingType ActionTypeToHoldingType(Task::ActionType t) {
	switch (t) {
	case Task::ActionType::MoveToLocation:
		return GameObj::HoldingType::AtLocation;
	case Task::ActionType::MoveInsideObj:
	case Task::ActionType::MakeCarriedBy:
		return GameObj::HoldingType::InObject;
	case Task::ActionType::MoveOntoObj:
		return GameObj::HoldingType::OnObject;
	case Task::ActionType::MakeWornBy:
		return GameObj::HoldingType::Worn;
	case Task::ActionType::MakePartOf:
		return GameObj::HoldingType::PartOf;
	default:
		throw std::logic_error("Invalid shortcut in action handling.");
	}
}

static inline constexpr Character::Posture ActionTypeToPosture(Task::ActionType t) {
	switch (t) {
	case Task::ActionType::MakeLyingOn:
		return Character::Posture::Lying;
	case Task::ActionType::MakeSittingOn:
		return Character::Posture::Sitting;
	case Task::ActionType::MakeStandingOn:
		return Character::Posture::Standing;
	default:
		throw std::logic_error("Invalid posture in posture action handling.");
	}
}

static inline constexpr GameObj::HoldingType ActionRefToHoldingType(Task::ActionRefType t) {
	switch (t) {
	case Task::ActionRefType::ObjsHeldBy:
	case Task::ActionRefType::ObjsInside:
	case Task::ActionRefType::CharsInside:
		return GameObj::HoldingType::InObject;
	case Task::ActionRefType::ObjsAtLocation:
	case Task::ActionRefType::CharsAtLocation:
		return GameObj::HoldingType::AtLocation;
	case Task::ActionRefType::ObjsOn:
	case Task::ActionRefType::CharsOn:
		return GameObj::HoldingType::OnObject;
	case Task::ActionRefType::ObjsWornBy:
		return GameObj::HoldingType::Worn;
	default:
		throw std::logic_error("Invalid shortcut in action ref handling.");
	}
}

static inline constexpr bool ObjIsAppropriate(Task::ActionRefType t, GameObj *o) {
	switch (t) {
	case Task::ActionRefType::ObjsHeldBy:
	case Task::ActionRefType::ObjsInside:
	case Task::ActionRefType::ObjsWornBy:
	case Task::ActionRefType::ObjsOn:
	case Task::ActionRefType::ObjsAtLocation:
	case Task::ActionRefType::ObjsWithProp:
		return !dynamic_cast<Character *>(o) && !dynamic_cast<Location *>(o);
	case Task::ActionRefType::CharsInside:
	case Task::ActionRefType::CharsOn:
	case Task::ActionRefType::CharsAtLocation:
	case Task::ActionRefType::CharsWithProp:
		return dynamic_cast<Character *>(o);
	case Task::ActionRefType::LocationOf:
	case Task::ActionRefType::LocationsInGroup:
	case Task::ActionRefType::LocationsWithProp:
		return dynamic_cast<Location *>(o);
	default:
		return false;
	}
}

void Task::Action::PerformImpl() {
	// at this stage, all references and lists are resolved.
	Game *g = Game::Get();
	std::string moveTarget;
	switch (type) {
	case ActionType::MoveToLocation:
	case ActionType::MoveInsideObj:
	case ActionType::MoveOntoObj:
	case ActionType::MakeCarriedBy:
	case ActionType::MakeWornBy:
	case ActionType::MakePartOf:
		moveTarget = rhs;
		goto ActionPerformMove;
	case ActionType::MoveToLocationOf:
		if (moveTarget.empty())  // jumped here directly rather than falling through
			moveTarget = g->GetObject(rhs)->GetParentKey();
		goto ActionPerformMove;
	case ActionType::MoveToParent:  // moving a character "up" one level
		if (moveTarget.empty()) {  // jumped here directly rather than falling through
			const std::string &parent = g->GetObject(lhs)->GetParentKey();
			if (!g->GetObject(parent)->GetParentKey().empty())
				moveTarget = g->GetObject(parent)->GetParentKey();
		}
		ActionPerformMove:
		switch (refType) {
		case ActionRefType::SingleObj:
			g->GetObject(lhs)->MoveTo(rhs, ActionTypeToHoldingType(type));
			break;
		case ActionRefType::ObjsHeldBy:
		case ActionRefType::ObjsInside:
		case ActionRefType::ObjsWornBy:
		case ActionRefType::ObjsOn:
		case ActionRefType::ObjsAtLocation:
		case ActionRefType::CharsInside:
		case ActionRefType::CharsOn:
		case ActionRefType::CharsAtLocation: {
			auto &allObjs = g->GetAllObjects();
			auto ht = ActionRefToHoldingType(refType);
			for (auto &o : allObjs) {
				if (ObjIsAppropriate(refType, o.second) && o.second->GetParentKey() == lhs && o.second->GetParentRelation() == ht)
					o.second->MoveTo(rhs, ActionTypeToHoldingType(type));
			}
		}
			break;
		case ActionRefType::ObjsWithProp:
		case ActionRefType::CharsWithProp: {
			auto &allObjs = g->GetAllObjects();
			auto propType = g->GetPropMeta(prop)->Type();
			switch (propType) {
			case Property::ValueType::Bool:
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<bool>(prop))
						o.second->MoveTo(rhs, ActionTypeToHoldingType(type));
				}
				break;
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<std::string>(prop) == rhs)
						o.second->MoveTo(rhs, ActionTypeToHoldingType(type));
				}
				break;
			case Property::ValueType::Map:
			case Property::ValueType::Int: {
				auto tmpInt = propType == Property::ValueType::Map ? ParseInt(rhs.c_str()) : g->GetExpression(expr)->EvaluateInt();
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<int32_t>(prop) == tmpInt)
						o.second->MoveTo(rhs, ActionTypeToHoldingType(type));
				}
				break;
			}
			case Property::ValueType::Text: {
				std::string tmpTxt(g->GetExpression(expr)->EvaluateStr());
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<std::string>(prop) == tmpTxt)
						o.second->MoveTo(rhs, ActionTypeToHoldingType(type));
				}
				break;
			}
			}
			break;
		}
		case ActionRefType::ObjsInGroup:
		case ActionRefType::CharsInGroup: {
			auto &allObjs = g->GetAllObjects();
			for (auto &o : allObjs) {
				if (ObjIsAppropriate(refType, o.second) && o.second->IsMemberOfGroup(lhs))
					o.second->MoveTo(rhs, ActionTypeToHoldingType(type));
			}
		}
			break;
		case ActionRefType::LocationOf:
		case ActionRefType::LocationsInGroup:
		case ActionRefType::LocationsWithProp:
			throw std::runtime_error("Task tried to move a location.");
		case ActionRefType::Task:
			throw std::runtime_error("Task tried to move a task.");
		case ActionRefType::None:
			throw std::runtime_error("Task tried to move nothing.");
		default:
			break;
		}
		break;
	case Starlane::Task::ActionType::MoveToGroup:
		break;
	case Starlane::Task::ActionType::MoveInDirection:
		break;
	case ActionType::AddToGroup:
	case ActionType::RemoveFromGroup:
	{
		// The only difference between these two types of actions is which function we call on the group,
		// so shorten this by storing the appropriate pointer now and then using it later:
		auto addOrRemove = type == ActionType::AddToGroup ? static_cast<void (Group:: *)(GameObj *)>(&Group::AddObj) : static_cast<void (Group:: *)(GameObj *)>(&Group::RemoveObj);
		auto grp = g->GetGroup(rhs);
		switch (refType) {
		case ActionRefType::SingleObj:
			g->GetObject(lhs)->MoveTo(rhs, ActionTypeToHoldingType(type));
			break;
		case ActionRefType::ObjsHeldBy:
		case ActionRefType::ObjsInside:
		case ActionRefType::ObjsWornBy:
		case ActionRefType::ObjsOn:
		case ActionRefType::ObjsAtLocation:
		case ActionRefType::CharsInside:
		case ActionRefType::CharsOn:
		case ActionRefType::CharsAtLocation: {
			auto &allObjs = g->GetAllObjects();
			auto ht = ActionRefToHoldingType(refType);
			for (auto &o : allObjs) {
				if (ObjIsAppropriate(refType, o.second) && o.second->GetParentKey() == lhs && o.second->GetParentRelation() == ht)
					(grp->*addOrRemove)(o.second);
			}
		}
			break;
		case ActionRefType::ObjsWithProp:
		case ActionRefType::CharsWithProp: {
			auto &allObjs = g->GetAllObjects();
			auto propType = g->GetPropMeta(prop)->Type();
			switch (propType) {
			case Property::ValueType::Bool:
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<bool>(prop))
						(grp->*addOrRemove)(o.second);
				}
				break;
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<std::string>(prop) == rhs)
						(grp->*addOrRemove)(o.second);
				}
				break;
			case Property::ValueType::Map:
			case Property::ValueType::Int: {
				auto tmpInt = propType == Property::ValueType::Map ? ParseInt(rhs.c_str()) : g->GetExpression(expr)->EvaluateInt();
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<int32_t>(prop) == tmpInt)
						(grp->*addOrRemove)(o.second);
				}
				break;
			}
			case Property::ValueType::Text: {
				std::string tmpTxt(g->GetExpression(expr)->EvaluateStr());
				for (auto &o : allObjs) {
					if (ObjIsAppropriate(refType, o.second) && o.second->GetPropValue<std::string>(prop) == tmpTxt)
						(grp->*addOrRemove)(o.second);
				}
				break;
			}
			}
			break;
		}
		case ActionRefType::ObjsInGroup:
		case ActionRefType::CharsInGroup:
		case ActionRefType::LocationOf:
		case ActionRefType::LocationsInGroup:
		case ActionRefType::LocationsWithProp: {
			auto &allObjs = g->GetAllObjects();
			for (auto &o : allObjs) {
				if (ObjIsAppropriate(refType, o.second) && o.second->IsMemberOfGroup(lhs))
					(grp->*addOrRemove)(o.second);
			}
		}
			break;
		case ActionRefType::Task:
			throw std::runtime_error("Task tried to move a task.");
		case ActionRefType::None:
			throw std::runtime_error("Task tried to move nothing.");
		default:
			break;
		}
		break;
	}
	case ActionType::MakeStandingOn:
	case ActionType::MakeSittingOn:
	case ActionType::MakeLyingOn:
		switch (refType) {
		case ActionRefType::SingleObj: {
			GameObj *theObj = Game::Get()->GetObject(lhs);
			Character *c;
			if (!(c = dynamic_cast<Character *>(theObj)))
				throw std::runtime_error("Tried to set the posture of a non-character");
			c->MakePosture(rhs, ActionTypeToPosture(type));
		}
			break;
		case ActionRefType::CharsAtLocation:
		case ActionRefType::CharsInside:
		case ActionRefType::CharsOn: {
			auto &allObjs = g->GetAllObjects();
			auto ht = ActionRefToHoldingType(refType);
			for (auto &o : allObjs) {
				Character *c;
				if ((c = dynamic_cast<Character *>(o.second)) && o.second->GetParentKey() == lhs && o.second->GetParentRelation() == ht)
					c->MakePosture(rhs, ActionTypeToPosture(type));
			}
		}
			break;
		case Starlane::Task::ActionRefType::CharsWithProp: {
			auto &allObjs = g->GetAllObjects();
			auto propType = g->GetPropMeta(prop)->Type();
			Character *c;
			switch (propType) {
			case Property::ValueType::Bool:
				for (auto &o : allObjs) {
					if ((c = dynamic_cast<Character *>(o.second)) && o.second->GetPropValue<bool>(prop))
						c->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			case Property::ValueType::Object:
			case Property::ValueType::Enum:
				for (auto &o : allObjs) {
					if ((c = dynamic_cast<Character *>(o.second)) && o.second->GetPropValue<std::string>(prop) == rhs)
						c->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			case Property::ValueType::Map:
			case Property::ValueType::Int: {
				auto tmpInt = propType == Property::ValueType::Map ? ParseInt(rhs.c_str()) : g->GetExpression(expr)->EvaluateInt();
				for (auto &o : allObjs) {
					if ((c = dynamic_cast<Character *>(o.second)) && o.second->GetPropValue<int32_t>(prop) == tmpInt)
						c->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			}
			case Property::ValueType::Text: {
				std::string tmpTxt(g->GetExpression(expr)->EvaluateStr());
				for (auto &o : allObjs) {
					if ((c = dynamic_cast<Character *>(o.second)) && o.second->GetPropValue<std::string>(prop) == tmpTxt)
						c->MakePosture(rhs, ActionTypeToPosture(type));
				}
				break;
			}
			}
			break;
		}
		case ActionRefType::CharsInGroup: {
			auto &allObjs = g->GetAllObjects();
			for (auto &o : allObjs) {
				Character *c;
				if ((c = dynamic_cast<Character *>(o.second)) && o.second->IsMemberOfGroup(lhs))
					c->MakePosture(rhs, ActionTypeToPosture(type));
			}
		}
			break;
		default:
			throw std::runtime_error("Tried to change the posture of a non-character.");
		}
		break;
	case Starlane::Task::ActionType::SetVarTo:
		break;
	case Starlane::Task::ActionType::IncVar:
		break;
	case Starlane::Task::ActionType::DecVar:
		break;
	case Starlane::Task::ActionType::SetPropTo:
		break;
	case Starlane::Task::ActionType::ExecTask:
		break;
	case Starlane::Task::ActionType::UnsetTask:
		break;
	case Starlane::Task::ActionType::SkipTurns:
		break;
	case Starlane::Task::ActionType::ConvoGreet:
		break;
	case Starlane::Task::ActionType::ConvoFarewell:
		break;
	case Starlane::Task::ActionType::ConvoAsk:
		break;
	case Starlane::Task::ActionType::ConvoTell:
		break;
	case Starlane::Task::ActionType::ConvoCmd:
		break;
	case Starlane::Task::ActionType::ConvoEnter:
		break;
	case Starlane::Task::ActionType::ConvoLeave:
		break;
	case Starlane::Task::ActionType::GameWin:
		break;
	case Starlane::Task::ActionType::GameLose:
		break;
	case Starlane::Task::ActionType::GameEndNeutral:
		break;
	case Starlane::Task::ActionType::GameContinue:
		break;
	default:
		break;
	}
}

}
