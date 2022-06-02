cmake_minimum_required(VERSION 3.8)

project(starlane_core CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTIONSION OFF)

add_library(starlane-core STATIC
	"starlane-core.cpp" "starlane-core.h" "slc_private_header.h")
target_compile_definitions(starlane-core PRIVATE SLC_BUILDING_SELF)