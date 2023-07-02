//
// Created by Adrian Welcker on 20.06.23.
//

#include "parser.h"

#include <stack>

namespace Starlane::Save {

// C++ queues annoyingly don't do destructive reads, so...
template<typename T> static inline T QueuePop(std::queue<T> &q) {
	T result = q.front();
	q.pop();
	return result;
}

static constexpr size_t tokensAtOnce = 50;

Parser::Parser(void *source) : hFile(source) {}
Parser::~Parser() {
	frontend->CloseFile(hFile);
}

inline AstNode* Parser::CreateNode() {
	// TODO: investigate if this is okay performance-wise, or whether we should be writing
	//  a proper arena allocator with placement new.
	if (nextNodeToUse == nullptr || nextNodeToUse > lastNodeInBlock) {
		nextNodeToUse = new AstNode[nodesAtOnce];
		lastNodeInBlock = nextNodeToUse + nodesAtOnce - 1;
		nodeStorageBlocks.push_back(nextNodeToUse);
	}
	return nextNodeToUse++;
}

static constexpr size_t readBufferSize = 1024*1024*10;  // 10MB
void Parser::Prepare() {  // TODO: this is terrible. not that i expect to ever have save files that large.
	auto incoming = static_cast<uint8_t *>(::operator new(readBufferSize));
	size_t read = frontend->ReadFile(hFile, incoming, readBufferSize);
	size_t counter = 1;
	while (read == counter * readBufferSize) {
		auto tmp = static_cast<uint8_t *>(::operator new((counter+1) * readBufferSize));
		memcpy(tmp, incoming, counter * readBufferSize);
		::operator delete(incoming);
		incoming = tmp;
		read += frontend->ReadFile(hFile, incoming + (counter * readBufferSize), readBufferSize);
		counter += 1;
	}
	fileContent = DoDecompression(incoming, read);
	::operator delete(incoming);
	prepared = true;
}

Token Parser::GetNextToken() {
	if (lexQueue.empty()) {
		Lex();
	}
	if (lexQueue.empty()) {
		throw SaveFileError { PE_UNEXPECTED_END, { TT_NONE, {{0}}} };
	}
	return QueuePop(lexQueue);
}

#define LexEOF() (position >= fileContent.size())
#define LexGetc() (fileContent[position++])

size_t Parser::Lex(size_t atLeast) {
	if (atLeast == 0) atLeast = tokensAtOnce;
	char buf[64];
	char c;
	TokenType assumption = TT_NONE;
	size_t tokensRead = 0;
	size_t len = 0;
	bool haveOpenQuote = false;
	bool comment = false;
	bool haveEscape = false;
	size_t lStringLen, lStringBegun;

	while (!LexEOF() && tokensRead < atLeast && lexQueue.size() < tokensAtOnce-1) {
		c = LexGetc();

		// if in comment, ignore everything until the end of the line
		if (comment) {
			if (c == '\n') {
				comment = false;
			}
			continue;
		}

		if (c == '\r') continue;

		if (haveOpenQuote || (!isspace(c) && c != '{' && c != '}' && c != '=' && c != '<' && c != '>' && c != '#')) {
			if (haveOpenQuote) {
				lStringLen += 1;
				if (!haveEscape) {
					haveEscape = (c == '\\');
				} else {
					haveEscape = false;
					continue;
				}
			}
			if (c == '"') {
				if (haveOpenQuote) {  // This ends a quoted string
					haveOpenQuote = false;
					goto finish_off_token;  // Immediately finish this token, don't add the quotation mark to the result.
				}
				else {  // This begins a quoted string
					haveOpenQuote = true;
					assumption = TT_LSTRING;
					lStringBegun = position+1;  // Don't add the quotation mark to the result
					lStringLen = 0;
					continue;
				}
			}
			if (len < 63) {
				buf[len] = c;
				buf[++len] = '\0';

				// update our assumption of what the currently-read token is, if necessary.
				if (assumption == TT_NONE) {
					if (isdigit(c) || c == '-') assumption = TT_INT;
					else assumption = TT_SSTRING;
				} else if (assumption == TT_INT && c == '.') {
					// probably an error, but...
					assumption = TT_SSTRING;
				}
			}
		} else { finish_off_token:
			Token token{};
			char *eptr;
			switch (assumption) {
				case TT_SSTRING:
					// Check if our "string" might be a bool after all
					if (STREQ(buf, "yes") || STREQ(buf, "YES")) {
						token.type = TT_BOOL;
						token.tok.Bool = true;
					}
					else if (STREQ(buf, "no") || STREQ(buf, "NO")) {
						token.type = TT_BOOL;
						token.tok.Bool = false;
					} else {  // nope, it really is a string
						token.type = TT_SSTRING;
						memcpy(token.tok.SString, buf, 64);
					}
					break;
				case TT_LSTRING:
					token.type = TT_LSTRING;
					token.tok.LString = { lStringBegun, lStringBegun+lStringLen };
					break;
				case TT_INT:
					token.type = TT_INT;
					token.tok.Int = strtoll(buf, &eptr, 10);
					if (token.tok.Int == 0 && eptr == buf) [[unlikely]] {
						token.type = TT_NONE;
						memcpy(token.tok.SString, buf, 64);
						throw SaveFileError { LE_INVALID_INT, token };
					}
					break;
				case TT_NONE:  // Special character
					break;
				default:
					// We never assign any other value, so this should never be reached.
					UNREACHABLE();
			}
			if (assumption != TT_NONE) {
				lexQueue.push(token);
				tokensRead++;
			}

			// Check whether the most recently read character (which caused the last token to be terminated)
			// is a special character. Note that whitespace is not itself a token.
			TokenType specialType = TT_NONE;
			if (c == '=') {
				specialType = TT_EQUALS;
			} else if (c == '{') {
				specialType = TT_OBRACE;
			} else if (c == '}') {
				specialType = TT_CBRACE;
			} else if (c == '#') {
				comment = true;
			}
			if (specialType != TT_NONE) {
				Token stok{};
				stok.type = specialType;
				lexQueue.push(stok);
				tokensRead++;
			}
			assumption = TT_NONE;
			buf[0] = '\0';
			len = 0;
		}
	}

	if (LexEOF()) {
		lexerDone = true;
		// produce an error if the file ended in the middle of a token.
		if (assumption != TT_NONE) {
			Token currentToken { TT_NONE, {{0}} };
			memcpy(currentToken.tok.SString, buf, 64);
			throw SaveFileError { PE_UNEXPECTED_END, currentToken };
		}
	}
	return tokensRead;
}

// Attempt to look ahead by 1 token.
TokenType Parser::Lookahead() {
	// Refill queue if necessary
	if (lexQueue.empty()) Lex();
	// Requested token beyond end of file
	if (lexQueue.empty()) return TokenType::TT_NONE;
	return lexQueue.front().type;
}

std::string Parser::MakeStringFromToken(const Token &t) {
	switch (t.type) {
		case TokenType::TT_SSTRING:
			return std::string(t.tok.SString);
		case TokenType::TT_LSTRING: {
			std::string result;
			bool haveEscape = false;
			for (size_t i = t.tok.LString.min; i < t.tok.LString.max; i++) {
				char currentChar = fileContent[i];
				if (haveEscape) {
					switch (currentChar) {
						case '\\':
						case '"':
							result += currentChar;
							break;
						case 'n':
							result += '\n';
							break;
						case 't':
							result += '\t';
							break;
						default:
							break;
					}
					haveEscape = false;
					continue;
				} else if (currentChar == '\\') {
					haveEscape = true;
					continue;
				} else {
					result += currentChar;
				}
			}
			return result;
		}
		case TokenType::TT_BOOL:
			return t.tok.Bool ? "yes" : "no";
		case TokenType::TT_INT:
			return std::to_string(t.tok.Int);
		case TokenType::TT_OBRACE:
		case TokenType::TT_CBRACE:
		case TokenType::TT_EQUALS:
		case TokenType::TT_NONE:
			return "";
		default:
			UNREACHABLE();
	}
	return "<invalid token>";  // shouldn't get here but MSVC is dumb, apparently.
}

// Represents the parser's internal state.
enum class State {
	CompoundRoot,
	HaveName,
	HaveNameEquals,
	HaveNameOpen,
	BegunIntList,
	BegunStringList,
	BegunBoolList
};

#define ADD_AS_CHILD(node) do { \
if (things.top()->sv.Child.first) { things.top()->sv.Child.last->nextSibling = (node); things.top()->sv.Child.last = (node); } \
else { things.top()->sv.Child.first = (node); things.top()->sv.Child.last = (node); } \
} while (0)

#define PARSE_ERROR(error) do { latestParserError = { (error), currentToken }; return nullptr; } while (0)

// This somewhat elephantine function is responsible for constructing the parse tree from the lexer output.
AstNode* Parser::Parse() {
	if (!prepared) Prepare();
	try {
		Lex();  // Initially fill token queue
	} catch (const SaveFileError &e) {
		latestParserError = e;
		return nullptr;
	}
	// Create a root node that will encompass the entire file.
	AstNode *root = CreateNode();
	root->type = NT_COMPOUND;
	root->myName = "tree_root";
	std::stack<AstNode *> things;  // Explicitly use a stack instead of using recursion.
	things.push(root);
	State state = State::CompoundRoot;
	Token currentToken = {TT_NONE, {{'\0'}}};

	while (!lexerDone || !lexQueue.empty()) {
		try {
			currentToken = GetNextToken();
		} catch (const SaveFileError &e) {
			if (lexerDone) break;  // in case the first token the lexer encounters is EOF
			latestParserError = e;
			return nullptr;
		}
		switch (state) {
			case State::CompoundRoot:
				if (currentToken.type == TT_SSTRING || currentToken.type == TT_LSTRING || currentToken.type == TT_INT) {
					state = State::HaveName;
					AstNode *nextNode = CreateNode();
					nextNode->myName = MakeStringFromToken(currentToken);
					ADD_AS_CHILD(nextNode);
					things.push(nextNode);
				} else if (currentToken.type == TT_CBRACE) {
					things.pop();
					if (things.empty()) PARSE_ERROR(PE_TOO_MANY_CLOSE_BRACES);
				} else {
					PARSE_ERROR(PE_INVALID_IN_COMPOUND);
				}
				break;
			case State::HaveName:  // Having read a name
				if (currentToken.type == TT_EQUALS) state = State::HaveNameEquals;
				else PARSE_ERROR(PE_INVALID_AFTER_NAME);
				break;
			case State::HaveNameEquals:  // Having read a name immediately followed by an equals sign
				switch (currentToken.type) {
					case TT_OBRACE:  // Could be compound or list
						state = State::HaveNameOpen;
						break;
					case TT_INT:  // something simple like "stuff = 30"
						state = State::CompoundRoot;
						things.top()->type = NT_INT;
						things.top()->sv.Int = currentToken.tok.Int;
						things.pop();
						break;
					case TT_BOOL:  // something even simpler like "stuff = yes"
						state = State::CompoundRoot;
						things.top()->type = NT_BOOL;
						things.top()->sv.Bool = currentToken.tok.Bool;
						things.pop();
						break;
					case TT_SSTRING:
					case TT_LSTRING:
						state = State::CompoundRoot;
						things.top()->type = NT_STRING;
						things.top()->Str = MakeStringFromToken(currentToken);
						things.pop();
						break;
					default:
						PARSE_ERROR(PE_INVALID_AFTER_EQUALS);
				}
				break;
			case State::HaveNameOpen:  // "stuff = {"
				switch (currentToken.type) {
					case TT_SSTRING:
					case TT_LSTRING:
						TokenType nextType;
						try {  // compound or string list
							nextType = Lookahead();
						} catch (const SaveFileError &e) {
							latestParserError = e;
							return nullptr;
						}
						if (nextType == TT_EQUALS) {
							things.top()->type = NT_COMPOUND;
							AstNode *nextNode = CreateNode();
							nextNode->type = NT_INDETERMINATE;
							state = State::HaveName;
							nextNode->myName = MakeStringFromToken(currentToken);
							ADD_AS_CHILD(nextNode);
							things.push(nextNode);
						} else if (Lookahead() == TT_SSTRING || Lookahead() == TT_LSTRING || Lookahead() == TT_CBRACE) {
							state = State::BegunStringList;
							things.top()->type = NT_STRINGLIST;
							AstNode *member = CreateNode();
							member->type = NT_STRINGLIST_MEMBER;
							member->Str = MakeStringFromToken(currentToken);
							ADD_AS_CHILD(member);
						} else PARSE_ERROR(PE_INVALID_COMBO_AFTER_OPEN);
						break;
					case TT_INT:
						try {
							if (Lookahead() == TT_INT || Lookahead() == TT_CBRACE) {
								state = State::BegunIntList;
								things.top()->type = NT_INTLIST;
								AstNode *member = CreateNode();
								member->type = NT_INTLIST_MEMBER;
								member->sv.Int = currentToken.tok.Int;
								ADD_AS_CHILD(member);
							} else if (Lookahead() == TT_EQUALS) {
								things.top()->type = NT_COMPOUND;
								AstNode *nextNode = CreateNode();
								nextNode->type = NT_INDETERMINATE;
								state = State::HaveName;
								nextNode->myName = MakeStringFromToken(currentToken);
								ADD_AS_CHILD(nextNode);
								things.push(nextNode);
							} else PARSE_ERROR(PE_INVALID_COMBO_AFTER_OPEN);
						} catch (const SaveFileError &e) {
							latestParserError = e;
							return nullptr;
						}
						break;
					case TT_BOOL: {
						state = State::BegunBoolList;
						things.top()->type = NT_BOOLLIST;
						AstNode *member = CreateNode();
						member->type = NT_BOOLLIST_MEMBER;
						member->sv.Bool = currentToken.tok.Bool;
						ADD_AS_CHILD(member);
					}
						break;
					case TT_CBRACE:
						state = State::CompoundRoot;
						things.top()->type = NT_EMPTY;
						things.pop();
						break;
					default:
						PARSE_ERROR(PE_INVALID_AFTER_OPEN);
				}
				break;
			// now follow the various list types, such as "stuff = { 1 2 3 }"
			case State::BegunIntList:
				if (currentToken.type == TT_CBRACE) {
					state = State::CompoundRoot;
					things.pop();
				} else if (currentToken.type == TT_INT) {
					AstNode *member = CreateNode();
					member->type = NT_INTLIST_MEMBER;
					member->sv.Int = currentToken.tok.Int;
					ADD_AS_CHILD(member);
				} else PARSE_ERROR(PE_INVALID_IN_INT_LIST);
				break;
			case State::BegunStringList:
				if (currentToken.type == TT_SSTRING || currentToken.type == TT_LSTRING) {
					AstNode *member = CreateNode();
					member->type = NT_STRINGLIST_MEMBER;
					member->Str = MakeStringFromToken(currentToken);
					ADD_AS_CHILD(member);
				} else if (currentToken.type == TT_CBRACE) {
					state = State::CompoundRoot;
					things.pop();
				} else PARSE_ERROR(PE_INVALID_IN_STRING_LIST);
				break;
			case State::BegunBoolList:
				if (currentToken.type == TT_BOOL) {
					AstNode *member = CreateNode();
					member->type = NT_BOOLLIST_MEMBER;
					member->sv.Bool = currentToken.tok.Bool;
					ADD_AS_CHILD(member);
				} else if (currentToken.type == TT_CBRACE) {
					state = State::CompoundRoot;
					things.pop();
				} else PARSE_ERROR(PE_INVALID_IN_BOOL_LIST);
				break;
		}
		if (things.empty()) {  // an extraneous closing brace caused our implicit root node to be closed
			PARSE_ERROR(PE_TOO_MANY_CLOSE_BRACES);
		}
	}

	// alternatively, if all input is consumed but the parser isn't "at rest"...
	if (things.size() > 1 || state != State::CompoundRoot) {
		PARSE_ERROR(PE_UNEXPECTED_END);
	}

	return root;
}

}