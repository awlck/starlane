#pragma once

#ifndef SLC_VALUEPARSERS_H
#define SLC_VALUEPARSERS_H

#include <stdexcept>
#include <utility>

namespace Starlane {

class ValueError: public std::runtime_error {
public:
	explicit ValueError(std::string type, std::string value) :
		type(std::move(type)), provided(std::move(value)),
		message(std::string("Invalid value \"") + provided + "\" for type `" + type + "`."),
		std::runtime_error(message) {}

	[[nodiscard]] const char *what() const override {
		return message.c_str();
	}

	std::string type;
	std::string provided;
	std::string message;
};

bool ParseBool(const char *txt);
int64_t ParseInt(const char *txt);

}

#endif  // !SLC_VALUEPARSERS_H
