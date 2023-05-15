//
// Created by Adrian Welcker on 15.05.23.
//
#pragma once

#ifndef SLC_EXPRESSIONS_BUILTINS_H
#define SLC_EXPRESSIONS_BUILTINS_H

#include "../expression.h"

namespace Starlane::Expr {

void EnsureString(Value &v);
void EnsureInt(Value &v, bool allowSigned = true);

enum class ListTransformType {
	None,  // take list entries at face value and don't perform any transformation
	Name   // take list entries as object keys and apply the `object.Name' function to each before formatting
};
// Takes a list in ADRIFT Expression list format (e.g., "foo|bar|baz") and returns
// a textual description, e.g., "foo, bar and baz"
std::string WriteListFrom(const std::string &lst, ListTransformType transform = ListTransformType::None);

}

#endif  // !SLC_EXPRESSIONS_BUILTINS_H
