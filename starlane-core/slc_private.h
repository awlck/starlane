// Forward-declarations used throughout starlane-core.

#pragma once

#ifndef SLC_PRIVATE_H
#define SLC_PRIVATE_H

#if !defined(NDEBUG)
#include <assert.h>
#define UNREACHABLE() assert(false && "reached presumed-unreachable code")
#elif defined(__cpp_lib_unreachable) && __cpp_lib_unreachable >= 202202L
#include <utility>
#define UNREACHABLE() std::unreachable()
#elif defined(_MSC_VER)
#define UNREACHABLE() __assume(false)
#elif (defined(__GNUC__) || defined(__clang__)) && __has_builtin(__builtin_unreachable)
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE() break
#endif

// gcc doesn't define `size_t' by default, so:
#include <stddef.h>

// A simplification of `strcmp`:
#include <string.h>
#define STREQ(a, b) (strcmp((a), (b)) == 0)

#include "starlane-core.h"

namespace pugi {
class xml_node;
}

namespace Starlane {
class Game;
class Description;
class Event;
class Group;
class Restriction;
class GameObj;
class Property;
class Task;
class UserFunction;
class Variable;
struct Expression;

// ID number of a description
// (with zero meaning "no text at all")
using DescrRef = size_t;
// ID number of a piece of plain text
using PlainTextRef = size_t;
// ID number of a restriction block
using RestrRef = size_t;
// ID number of an expression/function call
using ExprRef = size_t;

extern const Frontend *frontend;

enum class Pronoun {
	Subject,
	Object,
	Possessive,
	Reflective
};

namespace Save {
class Writer;
struct AstNode;
}

std::string DoDecompression(const uint8_t *data, size_t dataLen);

}

#endif  // !SLC_PRIVATE_H
