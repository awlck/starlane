cmake_minimum_required(VERSION 3.8)

project(starlane_core C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTIONSION OFF)

add_subdirectory(deps/pugixml)
add_library(puff STATIC "deps/puff/puff.c" "deps/puff/puff.h")

add_library(starlane-core STATIC
	"starlane-core.cpp" "starlane-core.h" "slc_private_header.h"
	"taffile.cpp" "deps/puff.h")
target_compile_definitions(starlane-core PRIVATE SLC_BUILDING_SELF)
target_link_libraries(starlane-core puff pugixml-static)