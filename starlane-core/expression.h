#pragma once

#ifndef SLC_EXPRESSIONS_H
#define SLC_EXPRESSIONS_H

#include "slc_private.h"

#include <map>
#include <stdexcept>
#include <string>

struct ast_node_tag;

namespace Starlane {
namespace Expr {
enum class ValueType {
	Invalid,
	Integer,
	String
};
struct Value {
	ValueType ty;
	int64_t Int;
	std::string Str;
	Value() : ty(ValueType::Invalid), Int(0) {};
	/* implicit */ Value(int64_t i) : ty(ValueType::Integer), Int(i) {};  // NOLINT
	/* implicit */ Value(const std::string &s) : ty(ValueType::String), Int(0), Str(s) {};  // NOLINT
	/* implicit */ Value(std::string &&s) : ty(ValueType::String), Int(0), Str(s) {};  // NOLINT
	Value(ValueType t, int64_t i, const std::string &s) : ty(t), Int(i), Str(s) {};
	explicit operator bool() const {
		switch (ty) {
		case ValueType::Integer:
			return Int != 0;
		case ValueType::String:
			return !Str.empty();
		default:
			throw std::logic_error("Tried to convert an invalid value.");
		}
	}
	bool operator ==(const Value &rhs) const {
		if (ty == ValueType::String && rhs.ty == ValueType::String)
			return Str == rhs.Str;
		if (ty == ValueType::Integer && rhs.ty == ValueType::Integer)
			return Int == rhs.Int;
		if (ty == ValueType::String && rhs.ty == ValueType::Integer)
			return Str == std::to_string(rhs.Int);
		if (ty == ValueType::Integer && rhs.ty == ValueType::String)
			return std::to_string(Int) == Str;
		throw std::runtime_error("Tried to equate an invalid value.");
	}
	bool operator <(const Value &rhs) const {
		if (ty == ValueType::Integer && rhs.ty == ValueType::Integer)
			return Int < rhs.Int;
		throw std::runtime_error("Tried to numerically compare non-integers.");
	}
};
}  // namespace Expr

struct Expression {
	Expression(const std::string &expr);
	~Expression();

	std::string exprStr;

	bool EvaluateBool() const { return EvaluateInt(); };
	int64_t EvaluateInt() const { if (constexType == ConstexprType::Int) return constValInt; return EvalAsIntImpl(); };
	std::string EvaluateStr() const;
	
	// parsing related stuff
	inline int GetNextChar() {
		if (position >= exprStr.length()) return EOF;
		return (unsigned char) exprStr[position++];
	}
	inline void *ParserAllocate(size_t bytes) {
		void *result = ::operator new(bytes);
		parserMemBlocks[result] = bytes;
		return result;
	}
	inline void *ParserReallocate(void *block, size_t newSize) {
		void *result = ::operator new(newSize);
		parserMemBlocks[result] = newSize;
		// we just allocated the new block, so it's a safe guess that they don't overlap.
		memcpy(result, block, parserMemBlocks[block]);
		parserMemBlocks.erase(block);
		::operator delete(block);
		return result;
	}
	inline void ParserFree(void *block) {
		parserMemBlocks.erase(block);
		::operator delete(block);
	}

	std::string_view GetNodeText(const ast_node_tag *node) const;
	ast_node_tag *CreateNode();
	const ast_node_tag *GetRootNode() const { return rootNode; };

private:
	size_t position = 0;

	ast_node_tag *firstNode = nullptr;
	ast_node_tag *lastNode = nullptr;
	ast_node_tag *rootNode = nullptr;

	std::map<void *, size_t> parserMemBlocks;

	enum class ConstexprType {
		None,
		Int,
		String
	};
	ConstexprType constexType = ConstexprType::None;
	int64_t constValInt;
	int64_t EvalAsIntImpl() const;

	Expr::Value EvalAnyNode(const ast_node_tag *node) const;
	Expr::Value EvalFunccall(Expr::Value toCall, const ast_node_tag *args) const;
	Expr::Value EvalItemfunc(Expr::Value obj, const ast_node_tag *toCall) const;

	// built-in functions
	Expr::Value LCaseImpl(const ast_node_tag *args) const;
	Expr::Value UCaseImpl(const ast_node_tag *args) const;
	Expr::Value PCaseImpl(const ast_node_tag *args) const;
	Expr::Value NumberAsTextImpl(const ast_node_tag *args) const;
	Expr::Value CharacterDescriptorImpl(const ast_node_tag *args) const;
	Expr::Value CharacterProperImpl(const ast_node_tag *args) const;
	Expr::Value DisplayObjectImpl(const ast_node_tag *args) const;
	Expr::Value AloneWithCharImpl(const ast_node_tag *args) const;
	Expr::Value LocationNameImpl(const ast_node_tag *args) const;
	Expr::Value TheObjectImpl(const ast_node_tag *args) const;
	Expr::Value CharacterNameImpl(const ast_node_tag *args) const;

	void PostProcessTree();

	static std::map<std::string, decltype(&Expression::LCaseImpl)> tableOfBuiltInFunctions;
};

}

#endif  // !SLC_EXPRESSIONS_H
