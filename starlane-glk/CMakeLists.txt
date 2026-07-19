cmake_minimum_required(VERSION 3.18)

project(starlane-glk C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SLGLK_LIB_ONLY "Glk frontend: compile to static library only, don't link with a Glk lib" ON)
option(SLGLK_USE_BUILTIN_CHEAPGLK "Glk Frontend: compile with the built-in cheapglk library" OFF)
if(NOT ${SLGLK_USE_BUILTIN_CHEAPGLK} AND NOT ${SLGLK_LIB_ONLY})
    option(SLGLK_GLK_LIB_NAME "Name of the external Glk library to use." "garglk")
endif()

add_library(starlane-glk-base STATIC
    starlane-glk.cpp
        glkextstubs.c
        glkext.h
)
target_link_libraries(starlane-glk-base starlane-core)
target_include_directories(starlane-glk-base PRIVATE "../starlane-core")

if(${SLGLK_LIB_ONLY} OR ${SLGLK_USE_BUILTIN_CHEAPGLK})
    target_include_directories(starlane-glk-base PRIVATE ../reference/cheapglk)
endif()

if(${SLGLK_USE_BUILTIN_CHEAPGLK})
    add_executable(starlane-glk
            ../reference/cheapglk/cgblorb.c
            ../reference/cheapglk/cgdate.c
            ../reference/cheapglk/cgfref.c
            ../reference/cheapglk/cggestal.c
            ../reference/cheapglk/cgmisc.c
            ../reference/cheapglk/cgschan.c
            ../reference/cheapglk/cgstream.c
            ../reference/cheapglk/cgstyle.c
            ../reference/cheapglk/cgunicod.c
            ../reference/cheapglk/cgwindow.c
            ../reference/cheapglk/gi_blorb.c
            ../reference/cheapglk/gi_dispa.c
            ../reference/cheapglk/gi_debug.c
            ../reference/cheapglk/main.c
    )
    target_link_libraries(starlane-glk starlane-glk-base)
endif()