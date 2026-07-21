#include "builtins.h"

#include "../expression.h"
#include "exprp_utility.h"
#include "../game.h"
#include "../random.h"
#include "../valueparsers.h"
#include "../gamecontent/character.h"
#include "../gamecontent/location.h"
#include "../gamecontent/utility.h"

namespace Starlane {
#define CHECK_ARGCOUNT(funcname, cnt) do { if (args->arity != (cnt)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " #cnt ", got " + std::to_string(args->arity)); } while (0)
#define CHECK_ARGCOUNT_V(funcname, min_, max_) do { if (args->arity < (min_) || args->arity > (max_)) throw std::runtime_error("Wrong number of arguments to built-in function " funcname ": expected " #min_ " to " #max_ ", got " + std::to_string(args->arity)); } while (0)

#define EXTRACT_FIRST_ARG_STR(from, to) \
	auto (to) = EvalAnyNode((from->child.first)); \
	Expr::EnsureString((to));

namespace Expr {

static const char *LanguageTens[] = { 0, 0, "twenty", "thirty", "fourty", "fifty", "sixty", "seventy", "eighty", "ninety" };
static const char *LanguageOnes[] = { 0, "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen" };

std::string LanguageNumber(int64_t num, bool f = false) {
	// adapted from Inform's implementation but extended for 64-bit integers
	// https://github.com/ganelson/inform/blob/3ea9ce725f0b06bd58a12f9dec7c6ec3afb0ef01/inform7/Internal/Inter/BasicInformKit/Sections/Paragraphing.i6t#L250
	if (num == 0) return "zero";
	std::string result;
	if (num < 0) {
		result = "minus ";
		num = -num;
	}
	if (num >= 1000000000000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000000000000);
		result += " quintillion";
		num /= 1000000000000000000;
		f = true;
	}
	if (num >= 1000000000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000000000);
		result += " quadrillion";
		num /= 1000000000000000;
		f = true;
	}
	if (num >= 1000000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000000);
		result += " trillion";
		num /= 1000000000000;
		f = true;
	}
	if (num >= 1000000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000000);
		result += " billion";
		num /= 1000000000;
		f = true;
	}
	if (num >= 1000000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000000);
		result += " million";
		num /= 1000000;
		f = true;
	}
	if (num >= 1000) {
		if (f) result += ", ";
		result += LanguageNumber(num % 1000);
		result += " thousand";
		num /= 1000;
		f = true;
	}
	if (num >= 100) {
		if (f) result += ", ";
		result += LanguageNumber(num % 100);
		result += " hundred";
		num /= 100;
		f = true;
	}
	if (num == 0) return result;
	if (f) result += " and ";
	if (num >= 20) {
		result += LanguageTens[num / 10];
		if (num%10 != 0) {
			result += ' ';
			result += LanguageNumber(num % 10);
		}
	} else {
		result += LanguageOnes[num];
	}
	return result;
}

// Takes a list in ADRIFT Expression list format (e.g., "foo|bar|baz") and returns
// a textual description, e.g., "foo, bar and baz".
// (The ADRIFT implementation of recursive list-writing is woefully broken, turning sibling items into child items.
//  We make an attempt to improve the presentation.)
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] std::string WriteListFrom(const std::string &lst, ListTransformType transform,
										ListJoinType join, bool recurse) {
	if (Util::StringIsNullOrWhitespace(lst))
		return "";
	std::string result;
	std::vector<std::string> entries(Util::SplitList(lst));

	size_t cnt = entries.size();
	for (size_t i = 0; i < cnt; i++) {
		if (i > 0) {
			if (i != cnt-1) {
				switch (join) {
				case ListJoinType::And:
				case ListJoinType::Or:
					result += ", ";
					break;
				case ListJoinType::Rows:
					result += '\n';
					break;
				}
 			} else if (i == cnt-1) {
				switch (join) {
				case ListJoinType::And:
					result += " and ";
					break;
				case ListJoinType::Or:
					result += " or ";
					break;
				case ListJoinType::Rows:
					result += '\n';
					break;
				}
			}
		}

		// Not every list holds object keys: a location's Exits, for one, is a list of direction
		// names, which have no object to look up and are simply written out as they stand.
		const auto *obj = Game::Get()->GetObject(entries[i]);

		switch (transform) {
		case ListTransformType::IndefName:
		case ListTransformType::DefName:
			if (obj) {
				result += obj->GetDisplayName(transform == ListTransformType::DefName);
				// Displaying a thing's name to the player means the player has now seen it.
				// (This is how, e.g., the contents of a just-opened container become "seen".)
				if (auto *pc = dynamic_cast<Character *>(Game::Get()->GetPlayerChar()))
					pc->MarkSeen(entries[i]);
				break;
			}
			// A direction name is written in lower case, as ADRIFT's own direction-list branch does.
			result += Util::ToLower(entries[i]);
			break;
		case ListTransformType::None:
			result += entries[i];
			break;
		}

		if (recurse && obj) {
			auto on = obj->GetListOfChildren(GameObj::ChildFilter::All, GameObj::ChildRelFilter::On, false);
			auto in = obj->GetListOfChildren(GameObj::ChildFilter::All, GameObj::ChildRelFilter::In, false);

			if (!on.empty()) {
				result += " (on which ";
				result += Util::IsList(on) ? "are " : "is ";
				result += WriteListFrom(on, transform, join, true);
				result += ')';
			}
			if (!in.empty()) {
				result += " (in which ";
				result += Util::IsList(in) ? "are " : "is ";
				result += WriteListFrom(in, transform, join, true);
				result += ')';
			}
		}
	}
	return result;
}

std::string ShowPronounForChar(const std::string &key, Pronoun pronoun) {
	auto *g = Game::Get();
	if (key == g->GetReference("%Player%") && g->GetPCReferralPerson() == ReferralPerson::FirstPerson) {
		switch (pronoun) {
		case Pronoun::Subject:
			return "I";
		case Pronoun::Object:
			return "me";
		case Pronoun::Possessive:
			return "my";
		case Pronoun::Reflective:
			return "myself";
		}
	} else if (key == g->GetReference("%Player%") && g->GetPCReferralPerson() == ReferralPerson::SecondPerson) {
		switch (pronoun) {
		case Pronoun::Subject:
		case Pronoun::Object:
			return "you";
		case Pronoun::Possessive:
			return "your";
		case Pronoun::Reflective:
			return "yourself";
		}
	} else {
		auto *c = g->GetObject(key);
		if (c->HasProp("Gender")) {
			if (c->GetStrProp("Gender") == "Male") {
				switch (pronoun) {
				case Pronoun::Subject:
					return "he";
				case Pronoun::Object:
					return "him";
				case Pronoun::Possessive:
					return "his";
				case Pronoun::Reflective:
					return "himself";
				}
			} else if (c->GetStrProp("Gender") == "Female") {
				switch (pronoun) {
				case Pronoun::Subject:
					return "she";
				case Pronoun::Object:
				case Pronoun::Possessive:
					return "her";
				case Pronoun::Reflective:
					return "herself";
				}
			}
		}
		// no gender property or unknown value
		switch (pronoun) {
		case Pronoun::Subject:
		case Pronoun::Object:
			return "it";
		case Pronoun::Possessive:
			return "its";
		case Pronoun::Reflective:
			return "itself";
		}
	}
	return "<invalid>";
}

void EnsureString(Value &v) {
	if (v.ty == ValueType::Integer) {
		v.ty = ValueType::String;
		v.Str = std::to_string(v.Int);
	} else if (v.ty == ValueType::Invalid)
		throw std::runtime_error("Invalid value.");
}

void EnsureInt(Value &v, bool allowSigned) {
	if (v.ty == Expr::ValueType::String) {
		size_t pos = 0;
		bool neg = false;
		while (isspace(v.Str[pos])) ++pos;
		if (allowSigned && v.Str[pos] == '-') {
			++pos;
			neg = true;
		}
		if (IsDigits(v.Str.c_str() + pos)) {
			v.ty = Expr::ValueType::Integer;
			v.Int = ParseInt(v.Str.c_str() + pos);
			if (neg) v.Int *= -1;
		} else {
			throw std::runtime_error("Got a string where a number was expected: \"" + v.Str + "\"");
		}
	} else if (v.ty == ValueType::Invalid) {
		throw std::runtime_error("Invalid value.");
	}
}

}  // namespace Expr

Expr::Value Expression::LCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("LCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return std::to_string(theArg.Int);
	else if (theArg.ty == Expr::ValueType::String) {
		return frontend->StrToLowerCase(theArg.Str);
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::UCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("UCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return std::to_string(theArg.Int);
	else if (theArg.ty == Expr::ValueType::String) {
		return frontend->StrToUpperCase(theArg.Str);
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::PCaseImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("PCase", 1);
	auto theArg = EvalAnyNode(args->child.first);
	if (theArg.ty == Expr::ValueType::Integer)
		return std::to_string(theArg.Int);
	else if (theArg.ty == Expr::ValueType::String) {
		return frontend->StrToSentenceCase(theArg.Str);
	} else throw std::runtime_error("Invalid value.");
}

Expr::Value Expression::NumberAsTextImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("NumberAsText", 1);
	auto theArg = EvalAnyNode(args->child.first);
	Expr::EnsureInt(theArg);
	return Expr::LanguageNumber(theArg.Int);
}

Expr::Value Expression::CharacterDescriptorImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("CharacterDescriptor", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	auto *theChar = dynamic_cast<Character *>(Game::Get()->GetObject(theArg.Str));
	if (theChar == nullptr)
		return Expr::Value();
	return theChar->GetDescriptor();
}

Expr::Value Expression::CharacterProperImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("CharacterProper", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	auto *theChar = dynamic_cast<Character *>(Game::Get()->GetObject(theArg.Str));
	if (theChar == nullptr)
		return Expr::Value();
	return theChar->GetProperName();
}

Expr::Value Expression::DisplayObjectImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("DisplayObject/DisplayCharacter", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	auto *theObj = Game::Get()->GetObject((theArg.Str));
	return theObj->GetDescription();
}

Expr::Value Expression::AloneWithCharImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("AloneWithChar", 0);
	auto g = Game::Get();
	auto player = g->GetPlayerChar();
	for (const auto &objref : g->GetAllObjects()) {
		if (Character *c = dynamic_cast<Character *>(objref.second)) {
			if (objref.second != player && c->GetLocationKey() == player->GetLocationKey())
				return c->Key();
		}
	}
	return Expr::Value();  // invalid
}

Expr::Value Expression::TurnsImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Turns", 0);
	// The cast picks Expr::Value's integer constructor: a turn count is a number, and games do
	// arithmetic on it as readily as they print it.
	return (int64_t) Game::Get()->GetTurnCount();
}

Expr::Value Expression::LocationNameImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("LocationName", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	auto *loc = dynamic_cast<Location *>(Game::Get()->GetObject(theArg.Str));
	if (!loc)
		return std::string("<invalid location>");
	return loc->GetDisplayName();
}

Expr::Value Expression::TheObjectImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("TheObject(s)", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	// Names the object and stops there: this is the function messages use to refer to a thing
	// mid-sentence ("%CharacterName% get[//s] out of %TheObject[%object%]%."), where reciting
	// what happens to be sitting in it -- "the duct (in which is Player)" -- is nonsense.
	// Listing contents is the business of `lst.List`, which asks for it explicitly.
	return Expr::WriteListFrom(theArg.Str, Expr::ListTransformType::DefName, Expr::ListJoinType::And, false);
}

// The %List...[key]% family: each names a thing and writes out some set of objects related to it,
// with indefinite articles and joined by "and" -- or the word "nothing" when the set is empty.
Expr::Value Expression::ListRelatedImpl(const ast_node_tag *args, ListRelation rel) const {
	CHECK_ARGCOUNT("List...", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	auto *g = Game::Get();
	std::string keys;
	switch (rel) {
	case ListRelation::Held:
	case ListRelation::Worn: {
		const auto *ch = dynamic_cast<const Character *>(g->GetObject(theArg.Str));
		if (!ch) return std::string("nothing");
		keys = ch->GetPossessionsList(rel == ListRelation::Worn ? Character::PossessionFilter::Worn
		                                                        : Character::PossessionFilter::Held,
		                              false);
		break;
	}
	case ListRelation::ObjectsIn:
	case ListRelation::ObjectsOnAndIn:
	case ListRelation::CharactersOnAndIn: {
		const auto *obj = g->GetObject(theArg.Str);
		if (!obj) return std::string("nothing");
		auto filter = rel == ListRelation::CharactersOnAndIn ? GameObj::ChildFilter::Characters
		                                                     : GameObj::ChildFilter::Objects;
		auto relFilter = rel == ListRelation::ObjectsIn ? GameObj::ChildRelFilter::In
		                                                : GameObj::ChildRelFilter::OnAndIn;
		keys = obj->GetListOfChildren(filter, relFilter, false);
		break;
	}
	}
	if (keys.empty()) return std::string("nothing");
	return Expr::WriteListFrom(keys, Expr::ListTransformType::IndefName, Expr::ListJoinType::And, false);
}

Expr::Value Expression::ListHeldImpl(const ast_node_tag *args) const {
	return ListRelatedImpl(args, ListRelation::Held);
}

Expr::Value Expression::ListWornImpl(const ast_node_tag *args) const {
	return ListRelatedImpl(args, ListRelation::Worn);
}

Expr::Value Expression::ListObjectsInImpl(const ast_node_tag *args) const {
	return ListRelatedImpl(args, ListRelation::ObjectsIn);
}

Expr::Value Expression::ListObjectsOnAndInImpl(const ast_node_tag *args) const {
	return ListRelatedImpl(args, ListRelation::ObjectsOnAndIn);
}

Expr::Value Expression::ListCharactersOnAndInImpl(const ast_node_tag *args) const {
	return ListRelatedImpl(args, ListRelation::CharactersOnAndIn);
}

void Expression::ApplyPronounKeyword(const std::string &kwRaw, Pronoun &pronoun, bool &force) const {
	std::string kw = Util::ToLower(kwRaw);
	if (kw == "force") force = true;
	else if (kw == "objective" || kw == "object" || kw == "target") pronoun = Pronoun::Object;
	else if (kw == "possessive" || kw == "possess") pronoun = Pronoun::Possessive;
	else if (kw == "reflective" || kw == "reflect") pronoun = Pronoun::Reflective;
	else if (kw == "subjective" || kw == "subject" || kw == "personal") pronoun = Pronoun::Subject;
	else throw std::runtime_error("Unexpected argument to character.Name: " + kwRaw);
}

void Expression::ParseCharacterPronounArgs(const ast_node_tag *first, Pronoun &pronoun, bool &force) const {
	for (const ast_node_tag *arg = first; arg; arg = arg->sibling.next) {
		auto tmp = EvalAnyNode(arg);
		Expr::EnsureString(tmp);
		ApplyPronounKeyword(tmp.Str, pronoun, force);
	}
}

Expr::Value Expression::DisplayCharacterName(const std::string &key, Pronoun pronoun, bool force) const {
	auto *g = Game::Get();
	bool usePronoun = force || key == g->GetReference("%Player%");
	// Checked regardless of the above: a character already named as Subject earlier this turn
	// upgrades a following Object request to Reflective ("he saw himself") whether or not `force`
	// or player-ness already decided to pronominalise on its own.
	if (auto prevPronoun = g->GetPronounMentionedThisTurn(key)) {
		usePronoun = true;
		if (*prevPronoun == Pronoun::Subject && pronoun == Pronoun::Object)
			pronoun = Pronoun::Reflective;
	}
	g->MentionCharacter(key, pronoun);
	if (usePronoun) return Expr::ShowPronounForChar(key, pronoun);
	auto *obj = g->GetObject(key);
	if (!obj) return Expr::Value();
	return obj->GetDisplayName(true);
}

Expr::Value Expression::CharNameImpl(const Character *ch, const ast_node_tag *args) const {
	CHECK_ARGCOUNT_V("character.Name", 0, 4);
	Pronoun pronoun = Pronoun::Subject;
	bool force = false;
	ParseCharacterPronounArgs(args ? args->child.first : nullptr, pronoun, force);
	return DisplayCharacterName(ch->Key(), pronoun, force);
}

Expr::Value Expression::CharacterNameImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT_V("CharacterName", 0, 5);
	auto g = Game::Get();
	std::string toDisplay;
	Pronoun pronoun = Pronoun::Subject;
	bool force = false;
	auto useDefaultCharacter = [&] {
		// if we are displaying a character's description, this reference will be set and we should
		// use that character.
		toDisplay = g->GetReference("<referral-character>");
		if (toDisplay.empty())  // default to the player character
			toDisplay = g->GetReference("%Player%");
	};
	if (args == nullptr || args->arity == 0) {
		useDefaultCharacter();
	} else {
		EXTRACT_FIRST_ARG_STR(args, theArg);
		// A lone argument naming a pronoun rather than a character key implicitly means whichever
		// character %CharacterName% would default to -- ADRIFT itself rewrites, e.g.,
		// "%CharacterName[objective]%" to "%CharacterName[%Player%, objective]%" for this reason,
		// and several real games rely on the shorthand verbatim.
		static const char *soloPronounWords[] = {
			"subject", "subjective", "personal", "target", "object", "objective", "possessive"
		};
		if (args->arity == 1 && Expr::IsListedIn(soloPronounWords, Util::ToLower(theArg.Str).c_str())) {
			useDefaultCharacter();
			ApplyPronounKeyword(theArg.Str, pronoun, force);
		} else {
			toDisplay = theArg.Str;
			ParseCharacterPronounArgs(args->child.first->sibling.next, pronoun, force);
		}
	}
	return DisplayCharacterName(toDisplay, pronoun, force);
}

Expr::Value Expression::LocationOfImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("LocationOfImpl", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	return Game::Get()->GetObject(theArg.Str)->GetLocationKey();
}

Expr::Value Expression::ParentOfImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("ParentOfImpl", 1);
	EXTRACT_FIRST_ARG_STR(args, theArg);
	return Game::Get()->GetObject(theArg.Str)->GetParentKey();
}

Expr::Value Expression::AbsImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Abs", 1);
	auto theArg = EvalAnyNode(args->child.first);
	Expr::EnsureInt(theArg);
	return theArg.Int < 0 ? (-theArg.Int) : theArg;
}

Expr::Value Expression::InstrImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Instr", 1);
	auto haystack = EvalAnyNode(args->child.first);
	Expr::EnsureString(haystack);
	auto needle = EvalAnyNode(args->child.last);
	Expr::EnsureString(needle);
	// this is a case-insensitive substring search, so...
	std::string theHaystack(frontend->StrToLowerCase(haystack.Str));
	std::string theNeedle(frontend->StrToLowerCase(needle.Str));
	const char *hs = theHaystack.c_str();
	const char *result = strstr(hs, theNeedle.c_str());
	if (result == nullptr) return 0;
	return (result - hs) + 1;
}

Expr::Value Expression::IfImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("If", 3);
	auto condition = bool(EvalAnyNode(args->child.first));
	if (condition) {
		return EvalAnyNode(args->child.first->sibling.next);  // second child
	} else {
		return EvalAnyNode(args->child.last);  // third child
	}
}

Expr::Value Expression::LeftImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Left", 2);
	EXTRACT_FIRST_ARG_STR(args, theString);
	auto theLength = EvalAnyNode(args->child.last);
	Expr::EnsureInt(theLength);
	return theString.Str.substr(0, theLength.Int);
}

Expr::Value Expression::LenImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Len", 1);
	EXTRACT_FIRST_ARG_STR(args, theString);
	return (int64_t) theString.Str.size();
}

Expr::Value Expression::MaxImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Max", 2);
	auto theFirst = EvalAnyNode(args->child.first);
	Expr::EnsureInt(theFirst);
	auto theSecond = EvalAnyNode(args->child.last);
	Expr::EnsureInt(theSecond);
	return theFirst < theSecond ? theSecond : theFirst;
}

Expr::Value Expression::MidImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Mid", 3);
	EXTRACT_FIRST_ARG_STR(args, theString);
	auto theStart = EvalAnyNode(args->child.first->sibling.next);
	Expr::EnsureInt(theStart, false);
	auto theLength = EvalAnyNode(args->child.last);
	Expr::EnsureInt(theLength, false);
	if (theStart.Int >= (int64_t)theString.Str.size())
		return std::string();
	return theString.Str.substr(theStart.Int, theLength.Int);
}

Expr::Value Expression::MinImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Min", 2);
	auto theFirst = EvalAnyNode(args->child.first);
	Expr::EnsureInt(theFirst);
	auto theSecond = EvalAnyNode(args->child.last);
	Expr::EnsureInt(theSecond);
	return theFirst < theSecond ? theFirst : theSecond;
}

Expr::Value Expression::OneOfImpl(const ast_node_tag *args) const {
	// The OneOf function technically accepts 'any number of parameters', but this is
	// probably a good sanity-check nonetheless.  Should anyone complain, it's easy
	// enough to raise this limit further.
	// (The hard limit is UINT32_MAX, which is the largest number the random-number
	//  generator can produce.)
	CHECK_ARGCOUNT_V("OneOf", 1, 9999);
	uint32_t rand = RandomInt(args->arity-1);
	uint32_t pos = 1;
	auto *resultNode = args->child.first;
	while (pos++ < rand && resultNode != nullptr)
		resultNode = resultNode->sibling.next;
	if (resultNode == nullptr)
		return Expr::Value();  // invalid
	// look at me, lazily evaluating alternatives!
	return EvalAnyNode(resultNode);
}

Expr::Value Expression::RandImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Rand", 2);
	auto lower = EvalAnyNode(args->child.first);
	Expr::EnsureInt(lower);
	auto upper = EvalAnyNode(args->child.last);
	return ((int64_t) RandomInt(upper.Int - lower.Int)) + lower.Int;
}

// https://stackoverflow.com/a/3418285
static void ReplaceAll(std::string &str, const std::string &from, const std::string &to) {
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}
}

Expr::Value Expression::ReplaceImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Replace", 3);
	EXTRACT_FIRST_ARG_STR(args, theString);
	auto theFind = EvalAnyNode(args->child.first->sibling.next);
	Expr::EnsureString(theFind);
	auto theReplace = EvalAnyNode(args->child.last);
	Expr::EnsureString(theReplace);
	ReplaceAll(theString.Str, theFind.Str, theReplace.Str);
	return theString;
}

Expr::Value Expression::RightImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Right", 2);
	EXTRACT_FIRST_ARG_STR(args, theString);
	auto theLength = EvalAnyNode(args->child.last);
	Expr::EnsureInt(theLength);
	auto pos = theString.Str.size() - theLength.Int;
	if (pos <= 0)
		return theString;
	return theString.Str.substr(pos);
}

Expr::Value Expression::StrImpl(const ast_node_tag *args) const {
	// this function is kinda pointless since Starlane already coerces values left and right
	CHECK_ARGCOUNT("Str", 1);
	EXTRACT_FIRST_ARG_STR(args, theResult);
	return theResult;
}

Expr::Value Expression::ValImpl(const ast_node_tag *args) const {
	CHECK_ARGCOUNT("Val", 1);
	auto theResult = EvalAnyNode(args->child.first);
	// special case: if `Val` fails a conversion, return 0 rather than throwing an error
	try {
		Expr::EnsureInt(theResult);
		return theResult;
	} catch (std::runtime_error &) {
		return 0;
	}
}

// `obj.Name` -- and, optionally, `obj.Name(indefinite)` / `obj.Name(none)`. ADRIFT's default here
// is the *definite* article ("the cell air duct"), not the indefinite one the object carries.
Expr::Value Expression::ObjNameImpl(const GameObj *obj, const ast_node_tag *args) const {
	if (args == nullptr || args->arity == 0)
		return obj->GetDisplayName(true);
	CHECK_ARGCOUNT_V("object.Name", 0, 3);
	// The arguments may come in any order, and only the article ones mean anything here.
	std::string txt;
	for (const ast_node_tag *arg = args->child.first; arg; arg = arg->sibling.next) {
		auto tmp = EvalAnyNode(arg);
		Expr::EnsureString(tmp);
		txt += Util::ToLower(tmp.Str);
		txt += ' ';
	}
	if (txt.find("indefinite") != std::string::npos) return obj->GetDisplayName(false);
	if (txt.find("none") != std::string::npos) return obj->GetBareName();
	return obj->GetDisplayName(true);
}

Expr::Value Expression::ObjChildrenImpl(const GameObj *obj, const ast_node_tag *args) const {
	if (args == nullptr || args->arity == 0)
		return obj->GetListOfChildren();
	CHECK_ARGCOUNT_V("object.Children", 0, 2);
	auto typeFilter = GameObj::ChildFilter::All;
	auto relationFilter = GameObj::ChildRelFilter::OnAndIn;
	if (args->arity == 1) {
		auto tmp = EvalAnyNode(args->child.first);
		Expr::EnsureString(tmp);
		const auto &txt = tmp.Str;
		if (txt == "Characters" || txt == "characters")
			typeFilter = GameObj::ChildFilter::Characters;
		else if (txt == "Objects" || txt == "objects")
			typeFilter = GameObj::ChildFilter::Objects;
		else if (txt == "In" || txt == "in")
			relationFilter = GameObj::ChildRelFilter::In;
		else if (txt == "On" || txt == "on")
			relationFilter = GameObj::ChildRelFilter::On;
		else if (txt != "All" && txt != "all" && txt != "OnAndIn" && txt != "onandin")
			throw std::runtime_error("Invalid filter in call to obj.Children: " + txt);
	} else if (args->arity == 2) {
		auto tmp1 = EvalAnyNode(args->child.first);
		Expr::EnsureString(tmp1);
		const auto &txt = tmp1.Str;
		if (txt == "Characters" || txt == "characters")
			typeFilter = GameObj::ChildFilter::Characters;
		else if (txt == "Objects" || txt == "objects")
			typeFilter = GameObj::ChildFilter::Objects;
		else if (txt != "All" || txt == "all")
			throw std::runtime_error("Invalid filter in call to obj.Children: " + txt);
		auto tmp2 = EvalAnyNode(args->child.first);
		Expr::EnsureString(tmp2);
		const auto &txt2 = tmp1.Str;
		if (txt2 == "In" || txt2 == "in")
			relationFilter = GameObj::ChildRelFilter::In;
		else if (txt2 == "On" || txt2 == "on")
			relationFilter = GameObj::ChildRelFilter::On;
		else if (txt2 != "OnAndIn" || txt2 == "onandin")
			throw std::runtime_error("Invalid filter in call to obj.Children: " + txt2);
	}
	return obj->GetListOfChildren(typeFilter, relationFilter);
}

Expr::Value Expression::ObjContentsImpl(const Starlane::GameObj *obj, const ast_node_tag *args) const {
	if (args == nullptr || args->arity == 0)
		return obj->GetListOfChildren(GameObj::ChildFilter::All, GameObj::ChildRelFilter::In);
	CHECK_ARGCOUNT_V("object.Contents", 0, 1);
	auto tmp = EvalAnyNode(args->child.first);
	Expr::EnsureString(tmp);
	const auto &txt = tmp.Str;
	auto typeFilter = GameObj::ChildFilter::All;
	if (txt == "Characters" || txt == "characters")
		typeFilter = GameObj::ChildFilter::Characters;
	else if (txt == "Objects" || txt == "objects")
		typeFilter = GameObj::ChildFilter::Objects;
	else if (txt != "All" || txt == "all")
		throw std::runtime_error("Invalid filter in call to obj.Contents: " + txt);
	return obj->GetListOfChildren(typeFilter, GameObj::ChildRelFilter::In);
}

static bool ValueAsBool(const Expr::Value &val) {
	if (val.ty == Expr::ValueType::String) {
		return ParseBool(val.Str.c_str());
	} else if (val.ty == Expr::ValueType::Integer) {
		return val.Int;
	} else throw std::runtime_error("Invalid value encountered.");
}

Expr::Value Expression::CharHeldImpl(const Starlane::Character *obj, const ast_node_tag *args) const {
	bool recurse = true;
	if (args != nullptr) {
		CHECK_ARGCOUNT_V("char.Held", 0, 1);
		auto tmp = EvalAnyNode(args->child.first);
		recurse = ValueAsBool(tmp);
	}
	return obj->GetPossessionsList(Character::PossessionFilter::Held, recurse);
}

Expr::Value Expression::CharWornImpl(const Starlane::Character *obj, const ast_node_tag *args) const {
	bool recurse = true;
	if (args != nullptr) {
		CHECK_ARGCOUNT_V("char.Worn", 0, 1);
		auto tmp = EvalAnyNode(args->child.first);
		recurse = ValueAsBool(tmp);
	}
	return obj->GetPossessionsList(Character::PossessionFilter::Worn, recurse);
}

Expr::Value Expression::CharWornAndHeldImpl(const Starlane::Character *obj, const ast_node_tag *args) const {
	bool recurse = true;
	if (args != nullptr) {
		CHECK_ARGCOUNT_V("char.WornAndHeld", 0, 1);
		auto tmp = EvalAnyNode(args->child.first);
		recurse = ValueAsBool(tmp);
	}
	return obj->GetPossessionsList(Character::PossessionFilter::WornAndHeld, recurse);
}


static const char *true_terms[] = { "True", "true", "TRUE", "Yes", "yes", "YES", "1" };
static const char *false_terms[] = { "False", "false", "FALSE", "No", "no", "NO", "0"};
static const char *and_terms[] = { "And", "and", "AND" };
static const char *or_terms[] = { "Or", "or", "OR" };
static const char *rows_terms[] = { "Rows", "rows", "ROWS" };
static const char *def_terms[] = { "Definite", "definite", "DEFINITE" };
static const char *indef_terms[] = { "Indefinite", "indefinite", "INDEFINITE" };
// starlane extension
static const char *notransform_terms[] = { "NoTransform", "notransform", "NOTRANSFORM" };

Expr::Value Expression::WriteListImpl(const std::string &lst, const ast_node_tag *args) const {
	bool recurse = true;
	auto transformType = Expr::ListTransformType::IndefName;
	auto joinType = Expr::ListJoinType::And;

	if (args != nullptr) {
		CHECK_ARGCOUNT_V("lst.List", 0, 3);
		// Parse arguments and set the above values. For extra fun, the arguments can be passed in any order.
		for (const ast_node_tag *arg = args->child.first; arg; arg = arg->sibling.next) {
			auto tmp = EvalAnyNode(arg);
			Expr::EnsureString(tmp);
			if (Expr::IsListedIn(true_terms, tmp.Str.c_str())) {
				recurse = true;
			} else if (Expr::IsListedIn(false_terms, tmp.Str.c_str())) {
				recurse = false;
			} else if (Expr::IsListedIn(and_terms, tmp.Str.c_str())) {
				joinType = Expr::ListJoinType::And;
			} else if (Expr::IsListedIn(or_terms, tmp.Str.c_str())) {
				joinType = Expr::ListJoinType::Or;
			} else if (Expr::IsListedIn(rows_terms, tmp.Str.c_str())) {
				joinType = Expr::ListJoinType::Rows;
			} else if (Expr::IsListedIn(indef_terms, tmp.Str.c_str())) {
				transformType = Expr::ListTransformType::IndefName;
			} else if (Expr::IsListedIn(def_terms, tmp.Str.c_str())) {
				transformType = Expr::ListTransformType::DefName;
			} else if (Expr::IsListedIn(notransform_terms, tmp.Str.c_str())) {
				transformType = Expr::ListTransformType::None;
			} else throw std::runtime_error("Unexpected argument to lst.List: " + tmp.Str);
		}
	}

	std::string result = Expr::WriteListFrom(lst, transformType, joinType, recurse);
	// ADRIFT's `.List` never comes out blank: an empty list reads as "nothing", so that
	// "...and are carrying %Player%.Held.List." is a sentence either way.
	if (result.empty()) return std::string("nothing");
	return result;
}

}