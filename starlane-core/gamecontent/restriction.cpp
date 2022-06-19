#include "restriction.h"

#include <stdexcept>

#include <pugixml.hpp>

#include "../game.h"

namespace Starlane {

namespace {

// Find the last occurrence of `c` in `str`, but no later than `maxidx`.
size_t LastIndexOf(const char *str, char c, size_t maxidx) {
	size_t result = (size_t) -1;
	for (size_t i = 0; i < maxidx && str[i]; i++) {
		if (str[i] == c) result = i;
	}
	return result;
}

// In ADRIFT5, like many languages, 'and' binds more strongly than 'or'.
// In order to simplify the interpreter logic below, we transform:
// #A#O# => (#A#)O#
// #A#A#O# => (#A#A#)O#
// (#O#)A#O# => ((#O#)A#)O#
// #A(#A#O#) => #A((#A#)O#)
// see: https://github.com/jcwild/ADRIFT-5/blob/a0042f1750e622eb75cbbf433ff8804f4ca11cc8/ADRIFT/clsUserSession.vb#L6736
void TransformRestrictionSequence(std::string &seq) {
	const char *c;
	while ((c = strstr(seq.c_str(), "A#O"))) {
		size_t i = c - seq.c_str();
		size_t j = LastIndexOf(seq.c_str(), '(', i);
		if (j == (size_t) -1) seq.insert(0, 1, '(');
		else seq.insert(j+1, 1, '(');
		// (i+2 in the original string, but we have inserted one character already.)
		seq.insert(i + 3, 1, ')');
	}
}

void SkipRemainderOfBlock(const std::string &seq, size_t &tidx, size_t &ridx, size_t bracketLevel) {
	size_t b = bracketLevel;
	while (tidx < seq.size() && b >= bracketLevel) {
		switch (seq[++tidx]) {
		case '(':
			b++;
			tidx++;
			break;
		case ')':
			tidx++;
			break;
		case '#':
			ridx++;
			tidx++;
			break;
		case 'A':
		case 'O':
			tidx++;
			break;
		}
	}
}

}  // anonymous namespace

Restriction *Restriction::CreateFromXML(const pugi::xml_node &xmlNode) {
	auto result = new Restriction;
	for (const auto &it : xmlNode.children("Restriction")) {
		Single s;
		const auto &txt = it.first_child();
		s.restrText.reserve(strlen(txt.child_value()) + 15);
		s.restrText = txt.name();
		s.restrText.append(" ").append(txt.child_value());
		const auto &msg = it.child("Message");
		if (msg.type() != pugi::node_null)
			s.failureMsg = Game::Get()->CreateDescFromXML(msg);
		s.Translate();
		result->restrs.emplace_back(std::move(s));
	}
	std::string sequence(xmlNode.child_value("BracketSequence"));
	TransformRestrictionSequence(sequence);
	result->sequence = sequence;
	return result;
}

std::pair<bool, DescrRef> Restriction::PassRestrictionBlock() const {
	size_t tidx = 0;
	size_t ridx = 0;
	return PassRestrictionBlock(tidx, ridx, 0);
}

std::pair<bool, DescrRef> Restriction::PassRestrictionBlock(size_t &tidx, size_t &ridx, size_t brackets) const {
	std::pair<bool, DescrRef> state;
	while (tidx < sequence.size()) {
		if (sequence[tidx] == '(') {
			state = PassRestrictionBlock(++tidx, ridx, brackets + 1);
			continue;
		} else if (sequence[tidx] == ')') {
			++tidx;
			return state;
		} else if ((sequence[tidx] == 'O' && state.first) || (sequence[tidx] == 'A' && !state.first)) {
			// `true OR any` and `false AND any` are always true or false, respectively. In those
			// cases, we are done with this block. Skip remainder, including any bracketed sub-blocks.
			// The reverse cases of `false OR any` and `true AND any` are not handled specially: they
			// simply continue through the loop.
			SkipRemainderOfBlock(sequence, tidx, ridx, brackets);
			return state;
		} else if (sequence[tidx] == '#') {
			// Check whether the particular restriction in the sequence passes and record the result.
			if (restrs[ridx].Pass()) {
				state = { true, 0 };
			} else {
				state = { false, restrs[ridx].failureMsg };
			}
			ridx++;
		} else {
			throw std::runtime_error("Unrecognized character in restriction sequence: " + sequence);
		}
		tidx++;
	}
	return state;
}

bool Restriction::Single::Pass() const {
	return true;
}

void Restriction::Single::Translate() {

}

}