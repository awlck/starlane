cmake_minimum_required (VERSION 3.10)

project("starlane-tools" LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(SL_BUILD_TOOLS)
	add_library(sl-txtdump "txtdump.cpp")
	target_link_libraries(sl-txtdump pugixml)
	target_include_directories(sl-txtdump PRIVATE ../starlane-core/deps/pugixml/src)

	add_library(sl-mapper "mapper.cpp")
	target_link_libraries(sl-mapper pugixml)
	target_include_directories(sl-mapper PRIVATE ../starlane-core/deps/pugixml/src)
	target_link_libraries(sl-mapper starlane-core)
	target_include_directories(sl-mapper PRIVATE ../starlane-core)

	add_executable(sltools "sltools.cpp")
	target_link_libraries(sltools sl-txtdump)
	target_link_libraries(sltools sl-mapper)
	target_link_libraries(sltools starlane-core)
	target_include_directories(sltools PRIVATE ../starlane-core)
endif()
