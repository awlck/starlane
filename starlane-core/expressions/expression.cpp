#include "../expression.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "exprp_utility.h"
#include "exprparser.h"
#include "builtins.h"
#include "../game.h"
#include "../valueparsers.h"
#include "../gamecontent/character.h"
#include "../gamecontent/description.h"
#include "../gamecontent/event.h"
#include "../gamecontent/location.h"
#include "../gamecontent/property.h"
#include "../gamecontent/userfunc.h"
#include "../gamecontent/utility.h"
#include "../gamecontent/variable.h"


#ifdef _MSC_VER
#include <Windows.h>
#undef GetObject
#endif

namespace Starlane {

#ifdef _MSC_VER
// armour against stack overflows due to small Windows stack
class ScopedSETranslator {
private:
	const _se_translator_function old_SE_translator;
public:
	ScopedSETranslator(_se_translator_function newTranslator) noexcept
		: old_SE_translator(_set_se_translator(newTranslator)) {}
	~ScopedSETranslator() noexcept { _set_se_translator(old_SE_translator); }
};

class StackOverflowError : public std::runtime_error {
public:
	StackOverflowError() : std::runtime_error("Stack overflow.") {}
};

void StackOverflowTranslator(unsigned int code, _EXCEPTION_POINTERS *) {
	if (code == EXCEPTION_STACK_OVERFLOW)
		throw StackOverflowError();
	throw std::runtime_error("SEH exception: " + std::to_string(code));
}
#endif

std::map<std::string, decltype(&Expression::LCaseImpl)> Expression::tableOfBuiltInFunctions
= {
	{ "LCase", &Expression::LCaseImpl },
	{ "LCASE", &Expression::LCaseImpl },
	{ "lcase", &Expression::LCaseImpl },
	{ "UCase", &Expression::UCaseImpl },
	{ "UCASE", &Expression::UCaseImpl },
	{ "ucase", &Expression::UCaseImpl },
	{ "PCase", &Expression::PCaseImpl },
	{ "PCASE", &Expression::PCaseImpl },
	{ "pcase", &Expression::PCaseImpl },
	{ "NumberAsText", &Expression::NumberAsTextImpl },
	{ "numberastext", &Expression::NumberAsTextImpl },
	{ "Numberastext", &Expression::NumberAsTextImpl },
	{ "NUMBERASTEXT", &Expression::NumberAsTextImpl },
	{ "CharacterDescriptor", &Expression::CharacterDescriptorImpl },
	{ "Characterdescriptor", &Expression::CharacterDescriptorImpl },
	{ "characterdescriptor", &Expression::CharacterDescriptorImpl },
	{ "CHARACTERDESCRIPTOR", &Expression::CharacterDescriptorImpl },
	{ "CharacterProper", &Expression::CharacterProperImpl },
	{ "Characterproper", &Expression::CharacterProperImpl },
	{ "characterproper", &Expression::CharacterProperImpl },
	{ "CHARACTERPROPER", &Expression::CharacterProperImpl },
	{ "DisplayCharacter", &Expression::DisplayObjectImpl },
	{ "Displaycharacter", &Expression::DisplayObjectImpl },
	{ "displaycharacter", &Expression::DisplayObjectImpl },
	{ "DISPLAYCHARACTER", &Expression::DisplayObjectImpl },
	{ "DisplayObject", &Expression::DisplayObjectImpl },
	{ "Displayobject", &Expression::DisplayObjectImpl },
	{ "displayobject", &Expression::DisplayObjectImpl },
	{ "DISPLAYOBJECT", &Expression::DisplayObjectImpl },
	{ "AloneWithChar", &Expression::AloneWithCharImpl },
	{ "Alonewithchar", &Expression::AloneWithCharImpl },
	{ "alonewithchar", &Expression::AloneWithCharImpl },
	{ "ALONEWITHCHAR", &Expression::AloneWithCharImpl },
	{ "LocationName", &Expression::LocationNameImpl },
	{ "Locationname", &Expression::LocationNameImpl },
	{ "locationname", &Expression::LocationNameImpl },
	{ "LOCATIONNAME", &Expression::LocationNameImpl },
	{ "TheObject", &Expression::TheObjectImpl },
	{ "Theobject", &Expression::TheObjectImpl },
	{ "theobject", &Expression::TheObjectImpl },
	{ "THEOBJECT", &Expression::TheObjectImpl },
	{ "TheObjects", &Expression::TheObjectImpl },
	{ "Theobjects", &Expression::TheObjectImpl },
	{ "theobjects", &Expression::TheObjectImpl },
	{ "THEOBJECTS", &Expression::TheObjectImpl },
	{ "CharacterName", &Expression::CharacterNameImpl },
	{ "Charactername", &Expression::CharacterNameImpl },
	{ "charactername", &Expression::CharacterNameImpl },
	{ "CHARACTERNAME", &Expression::CharacterNameImpl },
	{ "Abs", &Expression::AbsImpl },
	{ "ABS", &Expression::AbsImpl },
	{ "abs", &Expression::AbsImpl },
	{ "Instr", &Expression::InstrImpl },
	{ "INSTR", &Expression::InstrImpl },
	{ "instr", &Expression::InstrImpl },
	{ "If", &Expression::IfImpl },
	{ "IF", &Expression::IfImpl },
	{ "if", &Expression::IfImpl },
	{ "Left", &Expression::LeftImpl },
	{ "LEFT", &Expression::LeftImpl },
	{ "left", &Expression::LeftImpl },
	{ "Len", &Expression::LenImpl },
	{ "LEN", &Expression::LenImpl },
	{ "len", &Expression::LenImpl },
	{ "Max", &Expression::MaxImpl },
	{ "MAX", &Expression::MaxImpl },
	{ "max", &Expression::MaxImpl },
	{ "Mid", &Expression::MidImpl },
	{ "MID", &Expression::MidImpl },
	{ "mid", &Expression::MidImpl },
	{ "Min", &Expression::MinImpl },
	{ "MIN", &Expression::MinImpl },
	{ "min", &Expression::MinImpl },
	{ "OneOf", &Expression::OneOfImpl },
	{ "ONEOF", &Expression::OneOfImpl },
	{ "oneof", &Expression::OneOfImpl },
	{ "Oneof", &Expression::OneOfImpl },
	{ "Either", &Expression::OneOfImpl },  // 'Either' is just 'OneOf' with two alternatives, so...
	{ "EITHER", &Expression::OneOfImpl },
	{ "either", &Expression::OneOfImpl },
	{ "Rand", &Expression::RandImpl },
	{ "RAND", &Expression::RandImpl },
	{ "rand", &Expression::RandImpl },
	{ "Replace", &Expression::ReplaceImpl },
	{ "REPLACE", &Expression::ReplaceImpl },
	{ "replace", &Expression::ReplaceImpl },
	{ "Right", &Expression::RightImpl },
	{ "RIGHT", &Expression::RightImpl },
	{ "right", &Expression::RightImpl },
	{ "Str", &Expression::StrImpl },
	{ "STR", &Expression::StrImpl },
	{ "str", &Expression::StrImpl },
	{ "Val", &Expression::ValImpl },
	{ "VAL", &Expression::ValImpl },
	{ "val", &Expression::ValImpl }
};

// List of built-in item functions that require an object of any kind.
static const char *listOfObjectFunctions[] = {
		"Children",
		"Contents",
		"Description",
		"Location",
		"Name",
		"Parent"
};

// List of built-in item functions that require a location
static const char *listOfLocationFunctions[] = {
		"Exits",
		"Objects"
};

// List of built-in item functions that require a character
static const char *listOfCharacterFunctions[] = {
		"Descriptor",
		"Held",
		"ProperName",
		"Worn",
		"WornAndHeld"
};

// List of built-in item functions that require an event
static const char *listOfEventFunctions[] = {
		"Length",
		"Position"
};

// List of built-in item functions that require a list
static const char *listOfListFunctions[] = {
		"Count",
		"List"
};

Expression::Expression(const std::string &expr) : exprStr(expr), currentContext(nullptr) {
	if (expr.empty()) return;
	int ws = 0;
	while (isspace(expr[ws])) ++ws;
	if (IsDigits(expr.c_str()+ws)) {
		constexType = ConstexprType::Int;
		constValInt = ParseInt(expr.c_str()+ws);
		return;
	}
	exprp_context_t *ctx = exprp_create(this);
	int remaining = exprp_parse(ctx, &rootNode);
	if (remaining > 0 && !Util::StringIsNullOrWhitespace( std::string_view(expr).substr(expr.size()-remaining)))
		throw std::runtime_error("Did not consume entire expression while parsing: " + expr + "\n(" + std::to_string(remaining) + " chars remained.)");
	exprp_destroy(ctx);
	PostProcessTree();
}

Expression::~Expression() {
	for (auto &it : parserMemBlocks) {
		::operator delete(it.first);
	}
	ast_node_tag *next;
	for (ast_node_tag *p = firstNode; p != nullptr; p = next) {
		next = p->managed.next;
		delete p;
	}
}

void Expression::PostProcessTree() {
	// General functions without arguments are parsed like variables, we catch them here.
	for (ast_node_tag *node = firstNode; node != nullptr; node = node->managed.next) {
		if (node->type == AST_NODE_TYPE_VARIABLE && tableOfBuiltInFunctions.count(std::string(GetNodeText(node)))) {
			node->type = AST_NODE_TYPE_FUNCCALL;
			ast_node__append_child(node, system__create_ast_node_terminal(this, AST_NODE_TYPE_IDENTIFIER, node->range));
			ast_node__append_child(node, system__create_ast_node_terminal(this, AST_NODE_TYPE_FUNCARGS, range__void()));
		}
	}
}

std::string_view Expression::GetNodeText(const ast_node_tag *node) const {
	return std::string_view(exprStr).substr(node->range.min, node->range.max - node->range.min);
}

ast_node_tag *Expression::CreateNode() {
	ast_node_tag *node = new ast_node_tag;
	memset(node, 0, sizeof(ast_node_tag));
	node->system = this;
	if (lastNode == nullptr) {
		firstNode = node;
		lastNode = node;
	} else {
		node->managed.prev = lastNode;
		lastNode->managed.prev = node;
		lastNode = node;
	}
	return node;
}

class ContextMgr {
public:
	ContextMgr(Expression *e, const UserFuncContext *newContext)
		: theExpr(e), savedContext(e->currentContext)
	{
		e->currentContext = newContext;
	}
	~ContextMgr() {
		theExpr->currentContext = savedContext;
	}
private:
	Expression *theExpr;
	const UserFuncContext *savedContext;
};

std::string Expression::EvaluateStr(const UserFuncContext *context) {
	ContextMgr mgr(this, context);
#ifdef _MSC_VER
	// Windows apps can catch and recover from stack overflows using SEH.
	Expr::Value result;
	int resetSuccess = 0;
	bool stackDidOverflow = false;
	{
		ScopedSETranslator translator(&StackOverflowTranslator);
		try {
			result = EvalAnyNode(rootNode);
		} catch (const StackOverflowError &e) {
			// stack isn't actually unwound yet
			stackDidOverflow = true;
		}
		if (stackDidOverflow) {
			// *actual* catch logic
			resetSuccess = _resetstkoflw();
			if (resetSuccess) {  // reset succeeded, bail out of evaluating this
				frontend->OutputText("<i>(Internal error: expression too complex. Report this to whoever compiled this build of Starlane.)</i>");
				return "(expression too complex)";
			} else {
				// restore failed, crash after all
				RaiseFailFastException(NULL, NULL, FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);
			}
		}
	}
#else
	auto result = EvalAnyNode(rootNode);
#endif
	if (result.ty == Expr::ValueType::String) return result.Str;
	else if (result.ty == Expr::ValueType::Integer) return std::to_string(result.Int);
	throw std::runtime_error("Invalid expression result");
}

//NOLINTBEGIN(misc-no-recursion)
Expr::Value Expression::EvalAnyNode(const ast_node_tag *node) const {
	switch (node->type) {
	case AST_NODE_TYPE_IDENTIFIER:
	case AST_NODE_TYPE_STRING:
		return std::string(GetNodeText(node));
	case AST_NODE_TYPE_VARIABLE: {
		std::string nt(GetNodeText(node));
		if (currentContext) {
			if (auto f = currentContext->find(nt); f != currentContext->end())
				return f->second;
		}
		if (Util::IsReference(nt)) {
			return Game::Get()->GetReference(nt);
		} else if (Util::IsReference('%' + nt + '%')) {
			return Game::Get()->GetReference('%' + nt + '%');
		}
		auto theVar = Game::Get()->GetVarByName(nt);
		auto theType = theVar->GetType();
		if (theType == Variable::Type::String)
			return theVar->GetValue<std::string>();
		else if (theType == Variable::Type::Int)
			return theVar->GetValue<int64_t>();
		else throw std::logic_error("Wrong type of variable (presumed impossible).");
	}
	case AST_NODE_TYPE_INTEGER:
		return node->intVal;
	case AST_NODE_TYPE_OPERATOR_PLUS: {
		auto theVal = EvalAnyNode(node->child.first);
		if (theVal.ty == Expr::ValueType::String && IsDigits(theVal.Str.c_str())) {
			return ParseInt(theVal.Str.c_str());
		}
		if (theVal.ty == Expr::ValueType::Integer) return theVal;
		throw std::runtime_error("Trying to determine the integer value of a non-integer.");
	}
	case AST_NODE_TYPE_OPERATOR_MINUS: {
		auto theVal = EvalAnyNode(node->child.first);
		if (theVal.ty == Expr::ValueType::String && IsDigits(theVal.Str.c_str())) {
			return -ParseInt(theVal.Str.c_str());
		}
		if (theVal.ty == Expr::ValueType::Integer)
			return -theVal.Int;
		throw std::runtime_error("Trying to determine the negative value of a non-integer.");
	}
	case AST_NODE_TYPE_OPERATOR_NOT: {
		auto theVal = EvalAnyNode(node->child.first);
		return !bool(theVal);
	}
	case AST_NODE_TYPE_OPERATOR_ADD: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::String && rhs.ty == Expr::ValueType::String)
			return lhs.Str + rhs.Str;
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return lhs.Int + rhs.Int;
		throw std::runtime_error("Tried to add disjointed types.");
	}
	case AST_NODE_TYPE_OPERATOR_SUB: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return lhs.Int - rhs.Int;
		throw std::runtime_error("Tried to subtract non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_CONCAT: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::String && rhs.ty == Expr::ValueType::String)
			return lhs.Str + rhs.Str;
		if (lhs.ty == Expr::ValueType::String && rhs.ty == Expr::ValueType::Integer)
			return lhs.Str + std::to_string(rhs.Int);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::String)
			return std::to_string(lhs.Int) + rhs.Str;
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return std::to_string(lhs.Int) + std::to_string(rhs.Int);
		throw std::runtime_error("Tried to concatenate non-values.");
	}
	case AST_NODE_TYPE_OPERATOR_MUL: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer)
			return lhs.Int * rhs.Int;
		throw std::runtime_error("Tried to muliply non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_DIV: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer) {
			if (rhs.Int == 0) throw std::runtime_error("Tried to divide by zero.");
			return lhs.Int / rhs.Int;
		}
		throw std::runtime_error("Tried to divide non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_MOD: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer) {
			if (rhs.Int == 0) throw std::runtime_error("Tried to divide by zero.");
			return lhs.Int % rhs.Int;
		}
		throw std::runtime_error("Tried to modulo non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_POW: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		if (lhs.ty == Expr::ValueType::Integer && rhs.ty == Expr::ValueType::Integer) {
			if (rhs.Int == 0) return 1;
			if (lhs.Int == 0) return 0;
			return Expr::Value((int64_t) pow(lhs.Int, rhs.Int));
		}
		throw std::runtime_error("Tried to exponentiate non-integers.");
	}
	case AST_NODE_TYPE_OPERATOR_AND: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return lhs && rhs;
	}
	case AST_NODE_TYPE_OPERATOR_OR: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return lhs || rhs;
	}
	case AST_NODE_TYPE_OPERATOR_EQ: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return lhs == rhs;
	}
	case AST_NODE_TYPE_OPERATOR_NE: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return !(lhs == rhs);
	}
	case AST_NODE_TYPE_OPERATOR_LT: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return lhs < rhs;
	}
	case AST_NODE_TYPE_OPERATOR_LE: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return lhs < rhs || lhs == rhs;
	}
	case AST_NODE_TYPE_OPERATOR_GT: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return !(lhs < rhs) && !(lhs == rhs);
	}
	case AST_NODE_TYPE_OPERATOR_GE: {
		auto lhs = EvalAnyNode(node->child.first);
		auto rhs = EvalAnyNode(node->child.last);
		return !(lhs < rhs);
	}
	case AST_NODE_TYPE_FUNCCALL: {
		auto function = EvalAnyNode(node->child.first);
		if (node->child.last->type != AST_NODE_TYPE_FUNCARGS && node->child.last->type != AST_NODE_TYPE_TEXTCONTENT)
			throw std::runtime_error("Invalid node type on right-hand side of function call.");
		return EvalFunccall(function, node->child.last);
	}
	case AST_NODE_TYPE_ITEMFUNC: {
		auto object = EvalAnyNode(node->child.first);
		return EvalItemfunc(object, node->child.last);
	}
	case AST_NODE_TYPE_TEXTCONTENT: {
		std::string result;
		for (const auto *p = node->child.first; p != nullptr; p = p->sibling.next) {
			if (p->type == AST_NODE_TYPE_STRING) result += GetNodeText(p);
			else {
				auto v = EvalAnyNode(p);
				if (v.ty == Expr::ValueType::String) result += v.Str;
				else if (v.ty == Expr::ValueType::Integer) result += std::to_string(v.Int);
				else throw std::runtime_error("Invalid result type.");
			}
		}
		return result;
	}
	default:
		throw std::runtime_error("Can't deal with this node type at this time: " + std::to_string(node->type));
	}
}

Expr::Value Expression::EvalFunccall(Expr::Value toCall, const ast_node_tag *args) const {
	Expr::EnsureString(toCall);
	const std::string &func = toCall.Str;
	// idk, this seems more efficient than a chain of 20-or-so instances of 'if (func == "LCase")'
	if (tableOfBuiltInFunctions.count(func) > 0) {
		auto leFunction = tableOfBuiltInFunctions.at(func);
		return (this->*leFunction)(args);
	}

	auto *g = Game::Get();
	auto *v = g->GetVarByName(func);
	if (v) {
		auto varTy = v->GetType();
		if (varTy == Variable::Type::IntArray || varTy == Variable::Type::StringArray) {
			auto idxVal = EvalAnyNode(args->child.last);
			Expr::EnsureInt(idxVal, false);
			if (varTy == Variable::Type::IntArray)
				return v->GetValue<int64_t>((uint32_t) idxVal.Int);
			else
				return v->GetValue<std::string>((uint32_t) idxVal.Int);
		}
	}

	auto *udf = g->GetUserFuncByName(func);
	if (udf) {
		const auto &funcsig = udf->Signature();
		UserFuncContext udfArguments;
		if (args->arity > funcsig.size())
			return std::string("<invalid UDF call: too many arguments>");
		if (args->arity < funcsig.size())
			return std::string("<invalid UDF call: not enough arguments>");
		size_t i = 0;
		for (auto *a = args->child.first; a; a = a->sibling.next) {
			auto theArg = EvalAnyNode(a);
			if (funcsig[i].ty == UserFunction::ArgType::Number) {
				Expr::EnsureInt(theArg);
			} else {
				Expr::EnsureString(theArg);
			}
			udfArguments[funcsig[i].name] = theArg;
			i++;
		}
		return udf->Evaluate(udfArguments);
	}
	
	return Expr::Value();
}

Expr::Value Expression::EvalItemfunc(Expr::Value obj, const ast_node_tag *toCall) const {
	Expr::EnsureString(obj);
	// Extract function name and argument list, or note that there are no arguments.
	Expr::Value toCall_;
	const ast_node_tag *args;
	if (toCall->type == AST_NODE_TYPE_FUNCCALL) {
		toCall_ = EvalAnyNode(toCall->child.first);
		args = toCall->child.last;
	} else {
		toCall_ = EvalAnyNode(toCall);
		args = nullptr;
	}
	EnsureString(toCall_);
	auto *g = Game::Get();
	// Figure out what sort of function we're dealing with, and call it.
	if (Expr::IsListedIn(listOfObjectFunctions, toCall_.Str.c_str())) {
		const auto *theObj = g->GetObject(obj.Str);
		if (toCall_.Str == "Children")
			return ObjChildrenImpl(theObj, args);
		if (toCall_.Str == "Contents")
			return ObjContentsImpl(theObj, args);
		if (toCall_.Str == "Name")
			return theObj->GetDisplayName();
		if (toCall_.Str == "Description")
			return theObj->GetDescription();
		if (toCall_.Str == "Location")
			return theObj->GetLocationKey();
		if (toCall_.Str == "Parent")
			return theObj->GetParentKey();
	}
	if (Expr::IsListedIn(listOfLocationFunctions, toCall_.Str.c_str())) {
		const auto *theObj = dynamic_cast<Location *>(g->GetObject(obj.Str));
		if (!theObj) throw std::runtime_error("Item function on locations applied to non-location: " + obj.Str);
		if (toCall_.Str == "Objects")
			return theObj->GetListOfChildren(GameObj::ChildFilter::Objects, GameObj::ChildRelFilter::In);
		if (toCall_.Str == "Exits")
			return theObj->GetListOfExits();
	}
	if (Expr::IsListedIn(listOfCharacterFunctions, toCall_.Str.c_str())) {
		const auto *theObj = dynamic_cast<Character *>(g->GetObject(obj.Str));
		if (!theObj) throw std::runtime_error("Item function on characters applied to non-character: " + obj.Str);
		if (toCall_.Str == "Descriptor")
			return theObj->GetDescriptor();
		if (toCall_.Str == "ProperName")
			return theObj->GetProperName();
		if (toCall_.Str == "Held")
			return CharHeldImpl(theObj, args);
		if (toCall_.Str == "Worn")
			return CharWornImpl(theObj, args);
		if (toCall_.Str == "WornAndHeld")
			return CharWornAndHeldImpl(theObj, args);
	}
	if (Expr::IsListedIn(listOfEventFunctions, toCall_.Str.c_str())) {
		auto *theEvt = g->GetEvent(obj.Str);
		if (!theEvt) throw std::runtime_error("Item function on events applied to non-event: " + obj.Str);
		if (toCall_.Str == "Length")
			return theEvt->GetDuration().Value();
		if (toCall_.Str == "Position")
			return theEvt->GetTimeSinceStart();
	}
	if (toCall_.Str == "Count") {
		if (obj.Str.empty()) return 0;
		return std::count(obj.Str.begin(), obj.Str.end(), '|') + 1;
	}
	if (toCall_.Str == "List") {
		if (obj.Str.empty()) return std::string();
		return WriteListImpl(obj.Str, args);
	}
	if (toCall_.Str == "Sum") {
		if (obj.Str.empty()) return 0;
		int64_t result = 0;
		std::vector<std::string> nums = Util::SplitList(obj.Str);
		for (const auto &n : nums) {
			if (IsDigits(n.c_str()) || (n.size() >= 2 && n[0] == '-' && IsDigits(n.c_str()+1))) {
				result += ParseInt(n.c_str());
			} else throw std::runtime_error("Attempted to calculate the Sum of a list that isn't all numbers.");
		}
		return result;
	}

	// now for properties and stuff
	auto meta = g->GetPropMeta(toCall_.Str);
	if (!meta) {
		throw std::runtime_error("Not a property or built-in item function: " + toCall_.Str);
	}
	std::vector objsToConsider = Util::SplitList(obj.Str);
	std::string result;
	size_t cnt = 0;
	for (const auto &o: objsToConsider) {
		if (cnt > 0) result += '|';
		switch (meta->Type()) {
		case Property::ValueType::ErrorType:
			return Expr::Value();
		case Property::ValueType::Map:
		case Property::ValueType::Int:
			result += std::to_string(g->GetObject(o)->GetIntProp(toCall_.Str));
			break;
		case Property::ValueType::Enum:
		case Property::ValueType::Object:
			result += g->GetObject(o)->GetStrProp(toCall_.Str);
			break;
		case Property::ValueType::Text:
			result += g->GetDescription(g->GetObject(o)->GetIntProp(toCall_.Str))->Build();
			break;
		case Property::ValueType::Bool:  // bool properties act as filters rather than transforms, but only when operating on lists
			if (objsToConsider.size() == 1) {
				result = std::to_string(g->GetObject(o)->GetBoolProp(toCall_.Str));
				break;
			}
			if (g->GetObject(o)->GetBoolProp(toCall_.Str)) result += o;
			else continue;
			break;
		}
		++cnt;
	}
	return result;
}
//NOLINTEND(misc-no-recursion)

int64_t Expression::EvalAsIntImpl() const {
#ifdef _MSC_VER
	// Windows apps can catch and recover from stack overflows using SEH.
	Expr::Value result;
	int resetSuccess = 0;
	bool stackDidOverflow = false;
	{
		ScopedSETranslator translator(&StackOverflowTranslator);
		try {
			result = EvalAnyNode(rootNode);
		} catch (const StackOverflowError &e) {
			// stack isn't actually unwound yet
			stackDidOverflow = true;
		}
		if (stackDidOverflow) {
			// *actual* catch logic
			resetSuccess = _resetstkoflw();
			if (resetSuccess) {  // reset succeeded, bail out of evaluating this
				frontend->OutputText("<i>(Internal error: expression too complex. Report this to whoever compiled this build of Starlane.)</i>");
				return INT64_MIN;
			} else {
				// restore failed, crash after all
				RaiseFailFastException(NULL, NULL, FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);
			}
		}
	}
#else
	auto result = EvalAnyNode(rootNode);
#endif
	if (result.ty == Expr::ValueType::Invalid) throw std::runtime_error("Invalid expression result.");
	if (result.ty == Expr::ValueType::Integer) return result.Int;
	// we're dealing with a string, try to convert it to an integer.
	if (result.Str.empty()) return 0;  // hmm... not sure about this.
	Expr::EnsureInt(result);
	return result.Int;
}

}
