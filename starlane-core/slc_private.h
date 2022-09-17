// Forward-declarations used throughout starlane-core.

#pragma once

#ifndef SLC_PRIVATE_H
#define SLC_PRIVATE_H

// gcc doesn't define `size_t' by default, so:
#include <stddef.h>

// A simplification of `strcmp`:
#include <string.h>
#define STREQ(a, b) (strcmp((a), (b)) == 0)

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
}

#endif  // !SLC_PRIVATE_H
