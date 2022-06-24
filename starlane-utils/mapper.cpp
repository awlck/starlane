#include <pugixml.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <set>
#include <string.h>

#include <valueparsers.h>

std::string MakeNameSafe(const std::string &s, bool forCode = true) {
	std::string result;
	result.reserve(s.size());
	for (auto c : s) {
		switch (c) {
		case '[':
		case ']':
		case '"':
			continue;
		case '<':
		case '>':
		case '(':
		case ')':
			if (!forCode) result.append(1, c);
			continue;
		case ' ':
			if (forCode) result.append(1, '-');
		default:
			result.append(1, c);
			continue;
		}
	}
	if (Starlane::SkipText(result.c_str(), "Chapter ") || Starlane::SkipText(result.c_str(), "Section ")) {
		result.insert(0, 1, '_');
	}
	return result;
}

std::string CodeNameForKey(const std::string &s, const pugi::xml_document &doc) {
	const auto &ln = doc.select_node((std::string("/Adventure/Location[Key='") + s + "']/ShortDescription/Description[1]/Text").c_str()).node();
	return MakeNameSafe(ln.child_value()).append(1, ' ').append(s);
}


void mapper_run(const std::string &gametxt) {
	pugi::xml_document doc;
	auto parseResult = doc.load_string(gametxt.c_str());
	if (parseResult.status != pugi::status_ok) {
		throw std::runtime_error(parseResult.description());
	}

	auto gameNode = doc.child("Adventure");

		std::cout << '"' << gameNode.child_value("Title") << "\" by \"" << gameNode.child_value("Author") << "\"\n\n";

	for (const auto &loc : gameNode.children("Location")) {
		auto key = loc.child_value("Key");
		std::string name = MakeNameSafe(loc.child("ShortDescription").first_child().child_value("Text"), false);
		std::string cname = MakeNameSafe(loc.child("ShortDescription").first_child().child_value("Text"));
		std::cout << cname << ' ' << key << " is a room. The printed name is \"" << name << "\".\n";
		for (const auto &mov : loc.children("Movement")) {
			auto dir = mov.child_value("Direction");
			if (strcmp(dir, "Out") == 0) std::cout << "Outside";
			else if (strcmp(dir, "In") == 0) std::cout << "Inside";
			else std::cout << dir;
			std::cout << " from " << key << " is " << CodeNameForKey(mov.child_value("Destination"), doc) << ".\n";
		}
		std::cout << "\n";
	}
}
