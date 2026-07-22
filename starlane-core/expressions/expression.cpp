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
#include "../gamecontent/group.h"
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
	{ "DisplayLocation", &Expression::DisplayObjectImpl },
	{ "Displaylocation", &Expression::DisplayObjectImpl },
	{ "displaylocation", &Expression::DisplayObjectImpl },
	{ "DISPLAYLOCATION", &Expression::DisplayObjectImpl },
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
	{ "LocationOf", &Expression::LocationOfImpl },
	{ "Locationof", &Expression::LocationOfImpl },
	{ "locationof", &Expression::LocationOfImpl },
	{ "LOCATIONOF", &Expression::LocationOfImpl },
	{ "ParentOf", &Expression::ParentOfImpl },
	{ "Parentof", &Expression::ParentOfImpl },
	{ "parentof", &Expression::ParentOfImpl },
	{ "PARENTOF", &Expression::ParentOfImpl },
	{ "TheObject", &Expression::TheObjectImpl },
	{ "Theobject", &Expression::TheObjectImpl },
	{ "theobject", &Expression::TheObjectImpl },
	{ "THEOBJECT", &Expression::TheObjectImpl },
	{ "TheObjects", &Expression::TheObjectImpl },
	{ "Theobjects", &Expression::TheObjectImpl },
	{ "theobjects", &Expression::TheObjectImpl },
	{ "THEOBJECTS", &Expression::TheObjectImpl },
	{ "ListHeld", &Expression::ListHeldImpl },
	{ "Listheld", &Expression::ListHeldImpl },
	{ "listheld", &Expression::ListHeldImpl },
	{ "LISTHELD", &Expression::ListHeldImpl },
	{ "ListWorn", &Expression::ListWornImpl },
	{ "Listworn", &Expression::ListWornImpl },
	{ "listworn", &Expression::ListWornImpl },
	{ "LISTWORN", &Expression::ListWornImpl },
	{ "ListObjectsIn", &Expression::ListObjectsInImpl },
	{ "Listobjectsin", &Expression::ListObjectsInImpl },
	{ "listobjectsin", &Expression::ListObjectsInImpl },
	{ "LISTOBJECTSIN", &Expression::ListObjectsInImpl },
	{ "ListObjectsOnAndIn", &Expression::ListObjectsOnAndInImpl },
	{ "Listobjectsonandin", &Expression::ListObjectsOnAndInImpl },
	{ "listobjectsonandin", &Expression::ListObjectsOnAndInImpl },
	{ "LISTOBJECTSONANDIN", &Expression::ListObjectsOnAndInImpl },
	{ "ListCharactersOnAndIn", &Expression::ListCharactersOnAndInImpl },
	{ "Listcharactersonandin", &Expression::ListCharactersOnAndInImpl },
	{ "listcharactersonandin", &Expression::ListCharactersOnAndInImpl },
	{ "LISTCHARACTERSONANDIN", &Expression::ListCharactersOnAndInImpl },
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
	// Not something ADRIFT resolves in an expression at all -- it keeps a turn count but only
	// ever prints it from its own quit handler. Games write %Turns% into their own message text
	// regardless ("...after taking %Turns% turns."), where it would otherwise come out verbatim.
	{ "Turns", &Expression::TurnsImpl },
	{ "TURNS", &Expression::TurnsImpl },
	{ "turns", &Expression::TurnsImpl },
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
	exprp_parse(ctx, &rootNode);
	if (position < expr.size() && !Util::StringIsNullOrWhitespace( std::string_view(expr).substr(position)))
		throw std::runtime_error("Did not consume entire expression while parsing: " + expr + "\n(" + std::to_string(exprStr.size()-position) + " chars remained.)");
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

Expr::Value Expression::ResolveNameToValue(const std::string &nt) const {
	if (currentContext) {
		if (auto f = currentContext->find(nt); f != currentContext->end())
			return f->second;
	}
	if (Util::IsReference(nt)) {
		return Game::Get()->GetReference(nt);
	} else if (Util::IsReference('%' + nt + '%')) {
		return Game::Get()->GetReference('%' + nt + '%');
	}
	// A user-defined function that takes no arguments is written as a bare "%name%", which reads
	// as a name rather than as a call. (Return to the Stars names its rifle "%riflename%".)
	if (const UserFunction *udf = Game::Get()->GetUserFuncByName(nt)) {
		if (udf->Signature().empty())
			return udf->Evaluate({});
	}
	auto theVar = Game::Get()->GetVarByName(nt);
	// A name that is neither a reference nor a variable is nothing we can evaluate. Say so
	// rather than dereferencing the null we just got back: callers that can carry on without
	// this value (a task call's argument, say) are able to catch this, and the ones that
	// can't get a diagnosis naming the culprit instead of a segfault.
	if (!theVar)
		throw std::runtime_error("Not a known variable or reference: " + nt);
	auto theType = theVar->GetType();
	if (theType == Variable::Type::String)
		return theVar->GetValue<std::string>();
	else if (theType == Variable::Type::Int)
		return theVar->GetValue<int64_t>();
	else throw std::logic_error("Wrong type of variable (presumed impossible).");
}

std::string Expression::InterpolateRefs(const std::string &text) const {
	// Nothing to do -- and nothing to pay -- for the overwhelmingly common literal with no '%'.
	if (text.find('%') == std::string::npos)
		return text;
	std::string result;
	size_t i = 0;
	while (i < text.size()) {
		if (text[i] != '%') {
			result += text[i++];
			continue;
		}
		// A %reference% is %, then a name (letters, digits and the trailing punctuation ADRIFT
		// permits in variable names), then a closing %. The name is resolved exactly as a bare
		// %var% elsewhere in the expression would be.
		size_t close = text.find('%', i + 1);
		if (close != std::string::npos && close > i + 1) {
			const std::string name = text.substr(i + 1, close - (i + 1));
			try {
				// ResolveNameToValue takes the bare name, as a %var% elsewhere would after the
				// grammar strips its delimiters; it re-adds them itself for command/player refs.
				const Expr::Value v = ResolveNameToValue(name);
				result += v.ty == Expr::ValueType::Integer ? std::to_string(v.Int) : v.Str;
				i = close + 1;
				continue;
			} catch (const std::runtime_error &) {
				// Not a reference or variable after all (a literal "100%", say): leave the '%'
				// as written and carry on from just past it.
			}
		}
		result += text[i++];
	}
	return result;
}

//NOLINTBEGIN(misc-no-recursion)
Expr::Value Expression::EvalAnyNode(const ast_node_tag *node) const {
	switch (node->type) {
	case AST_NODE_TYPE_IDENTIFIER:
		return std::string(GetNodeText(node));
	case AST_NODE_TYPE_STRING:
		// A string literal can carry %reference%s that ADRIFT would have substituted before ever
		// parsing the expression (see InterpolateRefs); do the same now so a quoted reference
		// resolves rather than comparing as its own literal text.
		return InterpolateRefs(std::string(GetNodeText(node)));
	case AST_NODE_TYPE_VARIABLE:
		return ResolveNameToValue(std::string(GetNodeText(node)));
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
	// A group on the left of a '.' stands in for its members: expand it to the pipe-joined member
	// list so the function or property is applied to each and the results joined, as ADRIFT's
	// ReplaceOOProperty does. A plain key, or a value already carrying '|'-joined keys, is left as-is.
	std::string subject = obj.Str;
	if (const Group *grp = g->GetGroup(subject)) {
		subject.clear();
		for (const auto &m : grp->GetAllMembers()) {
			if (!subject.empty()) subject += '|';
			subject += m;
		}
	}

	// The aggregate functions look at the list as a whole rather than at each member.
	if (toCall_.Str == "Count") {
		if (subject.empty()) return 0;
		return std::count(subject.begin(), subject.end(), '|') + 1;
	}
	if (toCall_.Str == "List") {
		// An empty list still has a name: ADRIFT writes it out as "nothing".
		if (subject.empty()) return std::string("nothing");
		return WriteListImpl(subject, args);
	}
	if (toCall_.Str == "Sum") {
		if (subject.empty()) return 0;
		int64_t result = 0;
		for (const auto &n : Util::SplitList(subject)) {
			if (IsDigits(n.c_str()) || (n.size() >= 2 && n[0] == '-' && IsDigits(n.c_str()+1)))
				result += ParseInt(n.c_str());
			else throw std::runtime_error("Attempted to calculate the Sum of a list that isn't all numbers.");
		}
		return result;
	}

	// Everything else is per-member. A single subject keeps the function's typed result (so
	// arithmetic on %obj%.Weight still sees an integer); a genuine list is mapped and pipe-joined.
	std::vector<std::string> items = Util::SplitList(subject);
	if (items.empty())
		return std::string();
	if (items.size() == 1)
		return EvalItemfuncSingle(items[0], toCall_, args);

	// Mapping .Name over several objects yields prose meant for the player, so join it the way
	// ADRIFT's list writer does ("the ball, the box and the key") rather than with the internal '|'
	// separator. Without this a merged plural reference -- e.g. %objects%.Name after a multi-object
	// command aggregates -- would read "the ball|the box". Every other property keeps '|' so its
	// result can still feed a further list function. (.Name for characters routes through the same
	// token; EvalItemfuncSingle dispatches to CharNameImpl internally.)
	if (toCall_.Str == "Name") {
		std::vector<std::string> names;
		names.reserve(items.size());
		for (const auto &item : items) {
			Expr::Value v = EvalItemfuncSingle(item, toCall_, args);
			std::string s = (v.ty == Expr::ValueType::Integer) ? std::to_string(v.Int) : v.Str;
			if (!s.empty()) names.push_back(std::move(s));
		}
		std::string result;
		for (size_t i = 0; i < names.size(); i++) {
			if (i > 0)
				result += (i + 1 == names.size()) ? " and " : ", ";
			result += names[i];
		}
		return result;
	}

	// A bool property over a list filters it -- keeping the members it holds for -- rather than
	// mapping to a list of 0/1, which is what the old single-list property path did.
	const Property *meta = g->GetPropMeta(toCall_.Str);
	const bool boolFilter = meta && meta->Type() == Property::ValueType::Bool;
	std::string result;
	size_t cnt = 0;
	for (const auto &item : items) {
		Expr::Value v = EvalItemfuncSingle(item, toCall_, args);
		if (boolFilter) {
			const bool keep = (v.ty == Expr::ValueType::Integer) ? v.Int != 0 : !v.Str.empty();
			if (keep) {
				if (cnt++) result += '|';
				result += item;
			}
		} else {
			if (cnt++) result += '|';
			result += (v.ty == Expr::ValueType::Integer) ? std::to_string(v.Int) : v.Str;
		}
	}
	return result;
}

Expr::Value Expression::EvalItemfuncSingle(const std::string &key, const Expr::Value &toCall_, const ast_node_tag *args) const {
	auto *g = Game::Get();
	// A key that names nothing of the kind a function wants yields the empty string rather than
	// throwing, so the group/list mapping above can drop members the function doesn't apply to.
	if (Expr::IsListedIn(listOfObjectFunctions, toCall_.Str.c_str())) {
		const auto *theObj = g->GetObject(key);
		if (!theObj) return std::string();
		if (toCall_.Str == "Children") return ObjChildrenImpl(theObj, args);
		if (toCall_.Str == "Contents") return ObjContentsImpl(theObj, args);
		if (toCall_.Str == "Name") {
			// A character's Name honors pronoun arguments (Force/Objective/Possessive/...) and only
			// pronominalises if they've already been named this turn -- see DisplayCharacterName.
			if (const auto *ch = dynamic_cast<const Character *>(theObj))
				return CharNameImpl(ch, args);
			return ObjNameImpl(theObj, args);
		}
		if (toCall_.Str == "Description") return theObj->GetDescription();
		if (toCall_.Str == "Location") return theObj->GetLocationKey();
		if (toCall_.Str == "Parent") return theObj->GetParentKey();
	}
	if (Expr::IsListedIn(listOfLocationFunctions, toCall_.Str.c_str())) {
		const auto *theObj = dynamic_cast<Location *>(g->GetObject(key));
		if (!theObj) return std::string();
		if (toCall_.Str == "Objects")
			return theObj->GetListOfChildren(GameObj::ChildFilter::Objects, GameObj::ChildRelFilter::In);
		if (toCall_.Str == "Exits") return theObj->GetListOfExits();
	}
	if (Expr::IsListedIn(listOfCharacterFunctions, toCall_.Str.c_str())) {
		const auto *theObj = dynamic_cast<Character *>(g->GetObject(key));
		if (!theObj) return std::string();
		if (toCall_.Str == "Descriptor") return theObj->GetDescriptor();
		if (toCall_.Str == "ProperName") return theObj->GetProperName();
		if (toCall_.Str == "Held") return CharHeldImpl(theObj, args);
		if (toCall_.Str == "Worn") return CharWornImpl(theObj, args);
		if (toCall_.Str == "WornAndHeld") return CharWornAndHeldImpl(theObj, args);
	}
	if (Expr::IsListedIn(listOfEventFunctions, toCall_.Str.c_str())) {
		auto *theEvt = g->GetEvent(key);
		if (!theEvt) return std::string();
		if (toCall_.Str == "Length") return theEvt->GetDuration().Value();
		if (toCall_.Str == "Position") return theEvt->GetTimeSinceStart();
	}

	// now for properties and stuff
	auto meta = g->GetPropMeta(toCall_.Str);
	if (!meta)
		throw std::runtime_error("Not a property or built-in item function: " + toCall_.Str);
	const auto *theObj = g->GetObject(key);
	if (!theObj) return std::string();
	switch (meta->Type()) {
		case Property::ValueType::ErrorType:
			return Expr::Value();
		case Property::ValueType::Map:
		case Property::ValueType::Int:
		case Property::ValueType::Bool:
			return theObj->GetIntProp(toCall_.Str);
		case Property::ValueType::Enum:
		case Property::ValueType::Object:
			return theObj->GetStrProp(toCall_.Str);
		case Property::ValueType::Text:
			return g->GetDescription(theObj->GetIntProp(toCall_.Str))->Build();
	}
	return Expr::Value();
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
