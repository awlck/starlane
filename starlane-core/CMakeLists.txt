cmake_minimum_required(VERSION 3.8)

project(starlane_core C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTIONSION OFF)

add_subdirectory(deps/pugixml)
add_subdirectory(deps/miniz)

add_library(starlane-core STATIC
	"starlane-core.cpp" "starlane-core.h"
	"slc_private.h"
	"taffile.cpp" "taffile.h"
	"game.cpp" "game.h"
	"gameloader.cpp"
	"valueparsers.cpp" "valueparsers.h"
	"random.cpp" "random.h"
	"mechanus.h" "mechanus.cpp"
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
target_link_libraries(starlane-core miniz pugixml-static)
target_include_directories(starlane-core PRIVATE deps/miniz deps/pugixml/src)