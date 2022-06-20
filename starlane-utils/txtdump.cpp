

#include <pugixml.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>

void textdump_run(const std::string &gametxt) {
	
	pugi::xml_document doc;
	auto parseResult = doc.load_string(gametxt.c_str());
	if (parseResult.status != pugi::status_ok) {
		throw std::runtime_error(parseResult.description());
	}

	size_t count = 0;
	for (const auto &it: doc.select_nodes("//Description[local-name(..)!='ShortDescription']/Text")) {
		std::cout << "\n@@DESCRIPTION_TXT_" << ++count << "@@\n" << it.node().child_value() << "\n";
	}

	for (const auto &it: doc.select_nodes("//Variable[Type='Text']")) {
		count = 0;
		std::istringstream strm(it.node().child_value("InitialValue"));
		std::string s;
		while (std::getline(strm, s, '\n')) {
			std::cout << "\n@@VAR_" << it.node().child_value("Key") << "_TXT_" << ++count << "@@\n" << s.c_str() << "\n";
		}
	}
}