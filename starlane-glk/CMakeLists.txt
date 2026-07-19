cmake_minimum_required(VERSION 3.18)

project(starlane-glk C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SLGLK_LIB_ONLY "Glk frontend: compile to static library only, don't link with a Glk lib" ON)
option(SLGLK_USE_BUILTIN_CHEAPGLK "Glk Frontend: compile with the built-in cheapglk library" OFF)
if(NOT ${SLGLK_USE_BUILTIN_CHEAPGLK} AND NOT ${SLGLK_LIB_ONLY})
    # This must be a pkg-config module name, not a boolean, so `option()` (which coerces its
    # default to ON/OFF) doesn't fit here -- e.g. Gargoyle/Garglk installs a "garglk.pc" exposing
    # both libgarglk and the libgarglkmain that supplies main(). Point PKG_CONFIG_PATH at its
    # "lib/pkgconfig" directory if it isn't installed to a system-wide location.
    set(SLGLK_GLK_LIB_NAME "garglk" CACHE STRING "pkg-config module name of the external Glk library to use.")
endif()

add_library(starlane-glk-base STATIC
    starlane-glk.cpp
        glkext.h
)
target_link_libraries(starlane-glk-base starlane-core)
target_include_directories(starlane-glk-base PRIVATE "../starlane-core")

if(${SLGLK_LIB_ONLY} OR ${SLGLK_USE_BUILTIN_CHEAPGLK})
    target_include_directories(starlane-glk-base PRIVATE ../reference/cheapglk)

    # glkextstubs.c's fallback definitions are meant to be shadowed by a "real" Glk library that
    # implements them -- but that only works when the real definitions are directly-linked object
    # code (like cheapglk's, or whatever a SLGLK_LIB_ONLY consumer eventually links against),
    # since a symbol already resolved by object code never gets reconsidered against a dynamic
    # library's exports, regardless of __attribute__((weak)). A real external library (e.g.
    # Gargoyle's libgarglk.dylib, wired up below) already implements every one of these
    # extensions, so we deliberately don't compile our stand-ins in for that case -- linking both
    # would silently prefer our no-ops over the genuine implementation.
    target_sources(starlane-glk-base PRIVATE glkextstubs.c)
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

if(NOT ${SLGLK_LIB_ONLY} AND NOT ${SLGLK_USE_BUILTIN_CHEAPGLK})
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SLGLK_EXTERNAL_GLK REQUIRED IMPORTED_TARGET "${SLGLK_GLK_LIB_NAME}")
    pkg_get_variable(SLGLK_EXTERNAL_GLK_INCLUDEDIR "${SLGLK_GLK_LIB_NAME}" includedir)

    # Our sources #include "glk.h" etc. unprefixed (to stay agnostic of which Glk
    # implementation they end up linked against), but an installed external library
    # commonly nests its headers a subdirectory named after itself (e.g. Gargoyle's
    # "garglk.pc" only adds "-I<includedir>", expecting "#include <garglk/glk.h>").
    # Add both the bare includedir and the by-convention subdirectory so either layout works.
    target_include_directories(starlane-glk-base PRIVATE
            "${SLGLK_EXTERNAL_GLK_INCLUDEDIR}"
            "${SLGLK_EXTERNAL_GLK_INCLUDEDIR}/${SLGLK_GLK_LIB_NAME}")

    add_executable(starlane-glk external-glk-placeholder.cpp)
    target_link_libraries(starlane-glk PRIVATE starlane-glk-base PkgConfig::SLGLK_EXTERNAL_GLK)

    # A shared libgarglk built outside of any system library directory (the common case for a
    # locally-built checkout) needs an explicit runtime search path, or the linked executable
    # won't be able to find it at load time.
    target_link_options(starlane-glk PRIVATE "-Wl,-rpath,${SLGLK_EXTERNAL_GLK_LIBRARY_DIRS}")
endif()