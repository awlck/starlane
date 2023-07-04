//
// Created by Adrian Welcker on 20.06.23.
//
#pragma once

#ifndef SLC_SAVEFILES_READER_H
#define SLC_SAVEFILES_READER_H

#include "../slc_private.h"

#include <queue>
#include <stdexcept>
#include <vector>

#include "../expressions/exprp_utility.h"

#define ITERATE_CHILDREN(ofNode, target) for (const auto *target = ofNode->sv.Child.first; target; target = target->nextSibling)

namespace Starlane::Save {

enum TokenType {
	TT_SSTRING,
	TT_LSTRING,
	TT_OBRACE,
	TT_CBRACE,
	TT_EQUALS,
	TT_BOOL,
	TT_INT,
	TT_NONE
};

struct Token {
	TokenType type;
	union {
		char SString[64];
		range_t LString;
		bool Bool;
		int64_t Int;
	} tok;
};

enum NodeType {
	NT_INDETERMINATE = 0,
	NT_COMPOUND,
	NT_STRING,
	NT_BOOL,
	NT_INT,
	NT_INTLIST,
	NT_INTLIST_MEMBER,
	NT_BOOLLIST,
	NT_BOOLLIST_MEMBER,
	NT_STRINGLIST,
	NT_STRINGLIST_MEMBER,
	NT_EMPTY
};

struct AstNode {
	NodeType type = NodeType::NT_INDETERMINATE;
	std::string myName;
	AstNode *nextSibling = nullptr;

	std::string Str;
	union {
		int64_t Int;
		bool Bool;
		struct { AstNode *first, *last; } Child;
	} sv = {0};

	AstNode *FindChildByName(const char *name) const {
		if (type != NT_COMPOUND) return nullptr;
		for (AstNode *child = this->sv.Child.first; child; child = child->nextSibling) {
			if (child->myName == name)
				return child;
		}
		return nullptr;
	}
};

enum ParseErr {
	PE_NONE,
	PE_INVALID_IN_COMPOUND,
	PE_INVALID_AFTER_NAME,
	PE_INVALID_AFTER_EQUALS,
	PE_INVALID_AFTER_OPEN,
	PE_INVALID_COMBO_AFTER_OPEN,
	PE_INVALID_IN_INT_LIST,
	PE_INVALID_IN_STRING_LIST,
	PE_INVALID_IN_BOOL_LIST,
	PE_UNEXPECTED_END,
	PE_TOO_MANY_CLOSE_BRACES,
	LE_INVALID_INT
};

class SaveFileError : public std::runtime_error {
public:
	SaveFileError(ParseErr etype, const Token &erroredToken)
		: std::runtime_error(std::string("Error parsing save file: ") + std::to_string(etype)), etype(etype), erroredToken(erroredToken) {}

	ParseErr GetErrorType() const { return etype; }
	const Token &GetErroredToken() const { return erroredToken; }

private:
	ParseErr etype;
	Token erroredToken;
};

class Parser {
public:
	explicit Parser(void *source);
	~Parser();
	void Prepare();
	AstNode *Parse();
	SaveFileError GetLatestParserError() const { return latestParserError; }

private:
	void *hFile;
	std::string fileContent;
	bool prepared = false;
	SaveFileError latestParserError{ParseErr::PE_NONE, {TokenType::TT_NONE, {{0}}}};

	Token GetNextToken();
	size_t Lex(size_t atLeast = 0);
	TokenType Lookahead();
	AstNode *CreateNode();
	bool lexerDone = false;
	std::queue<Token> lexQueue;
	size_t position = 0;
	static constexpr size_t nodesAtOnce = 1024;
	std::vector<AstNode *> nodeStorageBlocks;
	AstNode *nextNodeToUse = nullptr;
	AstNode *lastNodeInBlock = nullptr;

	std::string MakeStringFromToken(const Token &t);
};

}

#endif  // !SLC_SAVEFILES_READER_H
