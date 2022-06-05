// Forward-declarations used throughout starlane-core.

#pragma once

/* A simple way to prevent internal starlane-core headers from being used
 * outside of the project. Include this header from any header not intended
 * for use by frontends.
 * 
 * The preprocessor symbol "SLC_BUILDING_SELF" is defined by CMake only when
 * compiling the starlane_core library itself. If it is not defined, the
 * preprocessor will encounter the "#error" directive within, aborting
 * compilation.
 */
#ifndef SLC_BUILDING_SELF
#error "Attempting to use a private starlane-core header outside of the project."
#endif  // !SLC_BUILDING_SELF

#ifndef SLC_PRIVATE_H
#define SLC_PRIVATE_H

namespace Starlane {
class Game;
class Description;
class Restriction;
class GameObj;
class Property;

// ID number of a description
using DescrRef = size_t;
// ID number of a restriction block
using RestrRef = size_t;
}

#endif  // !SLC_PRIVATE_H