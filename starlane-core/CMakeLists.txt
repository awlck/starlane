cmake_minimum_required(VERSION 3.10)

project(starlane_core C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(deps/pugixml)
add_subdirectory(deps/miniz)

# When cross-compiling, import the executable targets from a file
IF(CROSSCOMPILING)
	SET(IMPORT_EXECUTABLES "IMPORTFILE-NOTFOUND" CACHE FILEPATH "Point it to the export file from a native build")
	INCLUDE(${IMPORT_EXECUTABLES})
ELSE()  # Only build the generator if not crosscompiling
	add_executable(packcc "deps/packcc/src/packcc.c")
ENDIF()

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/exprparser.c" "${CMAKE_CURRENT_BINARY_DIR}/exprparser.h"
	COMMAND packcc ARGS -o "${CMAKE_CURRENT_BINARY_DIR}/exprparser" "${CMAKE_CURRENT_SOURCE_DIR}/expressions/exprgrammar.peg"
	MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/expressions/exprgrammar.peg"
)

add_library(slc_exprparser STATIC
	"${CMAKE_CURRENT_BINARY_DIR}/exprparser.c" "${CMAKE_CURRENT_BINARY_DIR}/exprparser.h"
)
target_include_directories(slc_exprparser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/expressions")
# ensure we can safely throw exeptions over the parser's C code
if (MSVC)
	target_compile_options(slc_exprparser PRIVATE "/EHc-")
else()
	target_compile_options(slc_exprparser PRIVATE "-fexceptions" "-Wno-unused-parameter")
endif()

add_library(starlane-core
	# core files
	"starlane-core.cpp" "starlane-core.h"
	"slc_private.h"
	"error.cpp" "error.h"
	"taffile.cpp"
	"game.cpp" "game.h" "gameloader.cpp" "parser.cpp"
	"valueparsers.cpp" "valueparsers.h"
	"random.cpp" "random.h"
	# expression handling
	"expressions/expression.cpp" "expression.h"
	"expressions/exprp_utility.cpp" "expressions/exprp_utility.h"
	"expressions/builtins.cpp" "expressions/builtins.h"
	# game content (objects, tasks et al.)
	"gamecontent/gameobj.h" "gamecontent/gameobj.cpp"
	"gamecontent/character.cpp" "gamecontent/character.h"
	"gamecontent/location.cpp" "gamecontent/location.h"
	"gamecontent/description.cpp" "gamecontent/description.h"
	"gamecontent/restriction.cpp" "gamecontent/restriction.h"
	"gamecontent/property.cpp" "gamecontent/property.h"
	"gamecontent/task.cpp" "gamecontent/task.h" 
	"gamecontent/group.cpp" "gamecontent/group.h"
	"gamecontent/propholder.cpp" "gamecontent/propholder.h"
	"gamecontent/variable.cpp" "gamecontent/variable.h"
	"gamecontent/event.cpp" "gamecontent/event.h"
	"gamecontent/walk.cpp" "gamecontent/walk.h"
	"gamecontent/userfunc.cpp" "gamecontent/userfunc.h"
	"gamecontent/utility.h" "gamecontent/utility.cpp"
	gamecontent/synonym.cpp gamecontent/synonym.h
	gamecontent/textoverride.cpp gamecontent/textoverride.h
	# save files
    savefiles/writer.cpp savefiles/writer.h
	savefiles/parser.cpp savefiles/parser.h)
# Reading game state hands back a `const T *`; writing goes through Game's Mutable* accessors,
# which is where the undo machinery lives (see game.h). A C-style cast quietly defeats that --
# `(GameObj *) g->GetObject(k)->GetLocation()` compiled happily until this was turned on -- so
# make dropping a qualifier that way an error rather than a habit. The deliberate route, the one
# const_cast in Game::Unconst, is unaffected: -Wcast-qual does not flag it.
if (NOT MSVC)
	target_compile_options(starlane-core PRIVATE "-Wcast-qual" "-Werror=cast-qual")
endif()

# UNDO records only what a turn changed (see Game::SaveUndo). Missing one of those changes would
# show up as an undo that quietly doesn't quite undo, three turns later -- so with this on, every
# undo point *also* keeps a whole-world copy, and restoring checks itself against it and stops
# dead, naming the slot, if the two disagree. Slow and memory-hungry by design; on in Debug, and
# available in any build for running the regression corpus under.
option(SL_UNDO_AUDIT "Cross-check every UNDO against a full snapshot" OFF)
if (SL_UNDO_AUDIT OR CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_compile_definitions(starlane-core PRIVATE SL_UNDO_AUDIT)
endif()

target_link_libraries(starlane-core miniz pugixml slc_exprparser)
if (WIN32)  # DbgHelp provides symbol resolution for captured backtraces
	target_link_libraries(starlane-core dbghelp)
endif()
target_include_directories(starlane-core PRIVATE deps/miniz deps/pugixml/src deps/magicenum ${CMAKE_CURRENT_BINARY_DIR})
if (BUILD_SHARED_LIBS)
	target_compile_definitions(starlane-core PUBLIC SL_SHARED_CORE)
endif()
if (MSVC)  # make sure SEH exceptions work
	string(REGEX REPLACE "/EHsc" "/EHa /EHc-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Export the parser generator binary for use by cross-compilation targets.
if(NOT CROSSCOMPILING)
	EXPORT(TARGETS packcc FILE "${CMAKE_BINARY_DIR}/ImportExecutables.cmake")
endif()
