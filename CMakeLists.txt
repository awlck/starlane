cmake_minimum_required (VERSION 3.16)

project("starlane" VERSION 0.1.0)

option(BUILD_SHARED_LIBS "Build the interpreter core as a shared library" OFF)
option(CROSSCOMPILING "Whether cross-compiling to a different platform" ${CMAKE_CROSSCOMPILING})
option(SL_BUILD_QT "Build the Starlane Qt frontend" ON)
option(SL_BUILD_CONSOLE "Build the Starlane console frontend" ON)
option(SL_BUILD_TOOLS "Build command-line utilities" ON)
option(SL_BUILD_C_WRAPPER "Build C-style API wrapper" ON)
option(SL_BUILD_GLK "Build the Starlane Glk frontend" ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT SL_IPO_SUPPORTED OUTPUT ipo_errors)

# Global compiler options: warnings, security, optimizations
if(MSVC)
	add_compile_options(
		"/we4716"  # treat "function does not return a value" as an error
		"/we4715"  # treat "not all control paths return a value" as an error
		# MSVC doesn't provide a falltrough warning during regular compilation.
		"/Zc:throwingNew"  # optimize for throwing `operator new`
		"/Zc:preprocessor"  # enable conforming preprocessor
		"/permissive-"  # enable conforming mode. You'd think this would be the default, but it isn't.
		"/Zf"  # improve debug info generation speed for parallel builds
		"/GS"  # enable the stack guard
		"/guard:cf"  # enable control flow guard for indirect calls
		"/guard:ehcont"  # enable exception continuation guards
	)
	add_link_options(
		"/guard:cf"  # linker support for indirect call guarding
		"/guard:ehcont"  # linker support for exception continuation guarding
	)
else()
	# GCC/clang
	add_compile_options(
		"-Wall"
		"-Wextra"
		"-Wimplicit-fallthrough"  # warn on implicit switch statement fallthrough
		"-Werror=return-type"  # treat return-related issues as errors
		"$<$<NOT:$<BOOL:${EMSCRIPTEN}>>:-fstack-protector>"  # enable stack protection
		"-fvisibility=hidden"
	)
endif()

if(NOT ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	if(${SL_IPO_SUPPORTED})
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
	else()
		message(WARNING "IPO/LTO/LTCG is not supported on this compiler: ${ipo_errors}")
	endif()
endif()

add_subdirectory("starlane-core")
add_subdirectory("starlane-utils")

if(SL_BUILD_QT)
	add_subdirectory("starlane-qt")
endif()

if(SL_BUILD_CONSOLE)
	add_subdirectory("starlane-console")
endif()

if(SL_BUILD_C_WRAPPER)
	add_subdirectory("slc-capi")
endif()

if(SL_BUILD_GLK)
	add_subdirectory("starlane-glk")
endif()

if(SL_BUILD_QT)
	include("cmake/Packaging.cmake")
endif()
