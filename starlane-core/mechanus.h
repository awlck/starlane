#pragma once

#ifndef SLC_MECHANUS_H
#define SLC_MECHANUS_H

#include "slc_private.h"

#include <assert.h>
#include <functional>
#include <variant>
#include <vector>

#include "game.h"

// In the Dungeons & Dragons RPG, Mechanus (aka Nirvana) is the lawful neutral-aligned
// outer plane, a place dedicated to rules, laws, and processes.
namespace Starlane::Mechanus {

/*   MECHANUS OPCODES.  */
enum class Opcode: uint8_t {
	// Nop: No operation at all
	Nop = 0,

	/*  CONTROL FLOW.  */
	// Label x: Label number `x`. Does not do anything in itself but serves as
	// a jump target.
	Label,
	// Jmpt x: Jump to label `x` if the topmost stack element is truthy.
	Jmpt,
	// Jmpf x: Jump to label `x` if the topmost stack element is falsy.
	Jmpf,
	// Ret: End execution and produce the top of the stack as the result.
	// -1 means the restriction passes. 0 means the restriction fails silenty.
	// Positive values mean the restriction fails with the specified description message.
	Ret,

	/*  ITERATORS.  */
	// IInit x: Initialize the iterator at local variable `x` and put the first
	// object on the stack.
	IInit,
	// INext x: Advance the iterator at local variable `x` and put the object
	// on the stack.
	INext,
	// Eq: Pushes true if the two top stack elements are equal, false otherwise.
	Eq,
	// Inv: Replaces the top stack element with its boolean inverse.
	Inv,

	/*  GAME CONTENT.  */
	// LdObj: load object reference for key on top of stack.
	LdObj,
	// GetProp x: Get property with key `x` for the object reference on top of the stack.
	GetProp,
	// LdVar x: Load variable reference for variable with key `x`
	LdVar,
	// GetVarVal: Get value for non-array variable reference on top of stack.
	GetVarVal,
	// GetArrVal: Get value at index on top of stack for array variable reference second on stack.
	GetArrVal,

	/*  STACK CONTROL.  */
	// Dup: Duplicates the top stack element.
	Dup,
	// Drop: Deletes the top stack element.
	Drop,
	// DropR x: Deletes the top `x` stack elements.
	DropR,
	// PushI x: Pushes the integer value `x`.
	PushI,
	// PushS x: Pushes the string `x`.
	PushS
};

// Ensure the entire instruction fits into a single 64-byte CPU cache line.
#pragma pack(push, 1)
struct Instruction {
	union {
		int64_t Int;
		char Str[63];
	} val;
	Opcode op;
};
#pragma pack(pop)

struct FilteredObjIter {
	using BaseIter = std::unordered_map<std::string, GameObj *>::const_iterator;
	using Predicate = std::function<bool(GameObj *)>;

	FilteredObjIter() = delete;
	// Store the predicate and create a FilteredObjIter object. The iterator
	// does not become valid until `Begin` is called.
	explicit FilteredObjIter(Predicate p) : pred(p) {}

	const GameObj *Begin(Predicate p) {
		base_iter = Game::Get()->objects.cbegin();
		if (p(base_iter->second))
			return base_iter->second;
		return Next();
	}

	const GameObj *Next() {
		while (++base_iter, !AtEnd()) {
			if (pred(base_iter->second))
				return base_iter->second;
		}
		return nullptr;
	}

	bool AtEnd() const { return base_iter == Game::Get()->objects.cend(); }

	BaseIter base_iter;
	Predicate pred;
};

struct Program {
	using var_t = std::variant<DescrRef, std::string, FilteredObjIter>;
	std::vector<Instruction> insns;
	std::unordered_map<size_t, var_t> localVars;
};

class ProgramBuilder {
public:
	// Allocate a new label ID (to be inserted into the program later)
	int64_t AllocateLabel() {
		assert(!done);
		return ++labelsSoFar;
	}
	// Insert an instruction to the end of the program.
	void InsertInsn(Instruction &&i) {
		assert(!done);
		insns.emplace_back(i);
	}
	// Create a local variable for the program and return its ID.
	size_t CreateLocal(Program::var_t var) {
		assert(!done);
		localVars[++localsSoFar] = var;
		return localsSoFar;
	}
	// Create a Program instance from the created instructions and local variables.
	Program Build() {
		assert(!done);
		done = true;
		return Program { insns, localVars };
	}
	
private:
	int64_t labelsSoFar = 0;
	size_t localsSoFar = 0;
	std::vector<Instruction> insns;
	std::unordered_map<size_t, Program::var_t> localVars;
	bool done = false;
};

// Which context is this program run in? (That is, what type of result is to be expected?)
enum class Context {
	// Evaluating a restriction: -1 for pass, 0 for silent failure,
	// positive for failure with a particular description.
	Restriction,
	// Evaluating an expression being assigned to an integer variable or property
	Int,
	// Evaluating an expression being assigned to a text variable
	String,
	// Evaluating text to be printed, within "<# #>" markers.
	Printing
};

int64_t Interpret(const Program &prog, Context c);

}

#endif  // !SLC_MECHANUS_H