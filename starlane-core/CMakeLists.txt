cmake_minimum_required(VERSION 3.8)

project(starlane_core C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(deps/pugixml)
add_subdirectory(deps/miniz)

# When cross-compiling, import the executable targets from a file
IF(CMAKE_CROSSCOMPILING)
	SET(IMPORT_EXECUTABLES "IMPORTFILE-NOTFOUND" CACHE FILEPATH "Point it to the export file from a native build")
	INCLUDE(${IMPORT_EXECUTABLES})
ENDIF()

# Only build the generator if not crosscompiling
IF(NOT CMAKE_CROSSCOMPILING)
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
	"taffile.cpp"
	"game.cpp" "game.h"
	"gameloader.cpp"
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
	"gamecontent/userfunc.cpp" "gamecontent/userfunc.h"
	"gamecontent/utility.h" "gamecontent/utility.cpp"
	# save files
    savefiles/writer.cpp savefiles/writer.h
	savefiles/parser.cpp savefiles/parser.h)
target_link_libraries(starlane-core miniz pugixml slc_exprparser)
target_include_directories(starlane-core PRIVATE deps/miniz deps/pugixml/src ${CMAKE_CURRENT_BINARY_DIR})
if (BUILD_SHARED_LIBS)
	target_compile_definitions(starlane-core PUBLIC SL_SHARED_CORE)
endif()
if (MSVC)  # make sure SEH exceptions work
	string(REGEX REPLACE "/EHsc" "/EHa /EHc-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Export the parser generator binary for use by cross-compilation targets.
if(NOT CMAKE_CROSSCOMPILING)
	EXPORT(TARGETS packcc FILE "${CMAKE_BINARY_DIR}/ImportExecutables.cmake")
endif()
