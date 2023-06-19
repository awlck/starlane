# CMakeList.txt: CMake-Projekt für "starlane". Schließen Sie die Quelle ein, und definieren Sie
# projektspezifische Logik hier.
#
cmake_minimum_required (VERSION 3.16)

project("starlane")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SHARED_LIBS "Build the interpreter core as a shared library" OFF)
option(SL_BUILD_QT "Build the Starlane Qt frontent" ON)
option(SL_BUILD_TOOLS "Build command-line utilities" ON)
option(SL_BUILD_C_WRAPPER "Build c-style API wrapper" ON)

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
		$<$<NOT:$<CONFIG:Debug>>:/GL>  # release only: whole program optimization
	)
	add_link_options(
		"/guard:cf"  # linker support for indirect call guarding
		"/guard:ehcont"  # linker support for exception continuation guarding
		$<$<NOT:$<CONFIG:Debug>>:/LTCG>  # linker support for whole program optimization
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
		$<$<NOT:$<CONFIG:Debug>>:-flto>  # release only: link-time whole-program optimization
	)
	add_link_options(
		$<$<NOT:$<CONFIG:Debug>>:-flto>
	)
endif()


add_subdirectory("starlane-core")
add_subdirectory("starlane-utils")

if(SL_BUILD_QT)
	add_subdirectory("starlane-qt")
endif()

if(SL_BUILD_C_WRAPPER)
	add_subdirectory("slc-capi")
endif()

if(MSVC)
	add_executable(sltest "testing.cpp")
	target_link_libraries(sltest starlane-core)
	target_include_directories(sltest PRIVATE starlane-core)
elseif(APPLE)
	enable_language(OBJCXX)
	add_executable(sltestMac "testingMac.mm")
	set_target_properties(sltestMac PROPERTIES LINKER_LANGUAGE OBJCXX)
	target_link_libraries(sltestMac starlane-core objc)
	target_link_libraries(sltestMac "-framework Foundation")
	target_compile_options(sltestMac PRIVATE "-fobjc-arc")
	target_include_directories(sltestMac PRIVATE starlane-core)
endif()