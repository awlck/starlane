cmake_minimum_required(VERSION 3.8)

project(starlane_core C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTIONSION OFF)

add_subdirectory(deps/pugixml)
add_subdirectory(deps/miniz)

add_executable(packcc "deps/packcc/src/packcc.c")

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

add_library(starlane-core STATIC
	# core files
	"starlane-core.cpp" "starlane-core.h"
	"slc_private.h"
	"taffile.cpp"
	"game.cpp" "game.h"
	"gameloader.cpp"
	"valueparsers.cpp" "valueparsers.h"
	"random.cpp" "random.h"
	# expression handling
	"expressions/expression.cpp" "expression.h"
	"expressions/exprp_utility.cpp" "expressions/exprp_utility.h"
	"expressions/builtins.cpp"
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
	"gamecontent/utility.h")
target_compile_definitions(starlane-core PRIVATE SLC_BUILDING_SELF)
target_link_libraries(starlane-core miniz pugixml-static slc_exprparser)
target_include_directories(starlane-core PRIVATE deps/miniz deps/pugixml/src ${CMAKE_CURRENT_BINARY_DIR})
