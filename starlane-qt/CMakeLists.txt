cmake_minimum_required(VERSION 3.10)

project(starlane_qt CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 COMPONENTS Core Gui Widgets Multimedia CONFIG REQUIRED)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(APPLE)
	# CMake's default Info.plist template substitutes these as plain
	# variables (not target properties), so they must be set() before
	# add_executable(... MACOSX_BUNDLE ...) below.
	set(MACOSX_BUNDLE_GUI_IDENTIFIER "de.diepixelecke.starlane")
	set(MACOSX_BUNDLE_BUNDLE_NAME "Starlane")
	# CMAKE_PROJECT_VERSION (not PROJECT_VERSION), since this directory's
	# own project(starlane_qt) call above shadows it with an unversioned one.
	set(MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
	set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}")
endif()

# Needed on macOS for macdeployqt (and to get a real .app instead of a bare
# Mach-O executable): it only knows how to deploy into a bundle. Ignored on
# other platforms.
add_executable(starlane WIN32 MACOSX_BUNDLE "starlane.cpp" mainwindow.cpp mainwindow.h outputformatter.cpp outputformatter.h blorbfile.cpp blorbfile.h)
target_link_libraries(starlane Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia starlane-core)
# ../starlane-core: the core's public API. ${CMAKE_BINARY_DIR}/generated:
# starlane-version.h (see the top-level CMakeLists.txt), for a future About
# dialog to report the running version from.
target_include_directories(starlane PRIVATE "../starlane-core" "${CMAKE_BINARY_DIR}/generated")

if(APPLE)
	# Registers the .taf extension (see packaging/Info.plist.in) so Finder can hand game files to
	# us via "open with"/double-click/drag-onto-dock-icon, delivered as a QFileOpenEvent.
	set_target_properties(starlane PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in")
endif()

install(TARGETS starlane
	RUNTIME DESTINATION bin
	BUNDLE DESTINATION .
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	install(FILES packaging/starlane.desktop DESTINATION share/applications)
endif()
