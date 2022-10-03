#pragma once

#ifndef SLC_EXPRESSIONS_H
#define SLC_EXPRESSIONS_H

#include <map>
#include <string>

struct ast_node_tag;

namespace Starlane {

struct Expression {
	Expression(const std::string &expr);
	~Expression();

	std::string exprStr;

	// TODO
	bool EvaluateBool() const { return false; };
	int64_t EvaluateInt() const { if (constexType == ConstexprType::Int) return constValInt; return 0; };
	std::string EvaluateStr() const { return exprStr; };
	
	// parsing related stuff
	inline int GetNextChar() {
		if (position >= exprStr.length()) return EOF;
		return exprStr[position++];
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

private:
	size_t position = 0;

	ast_node_tag *firstNode = nullptr;
	ast_node_tag *lastNode = nullptr;

	std::map<void *, size_t> parserMemBlocks;

	enum class ConstexprType {
		None,
		Int,
		String
	};
	ConstexprType constexType;
	int64_t constValInt;
};

}

#endif  // !SLC_EXPRESSIONS_H