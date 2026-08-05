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

	# For converting the app icon to Apple's format:
	find_program(ICONUTIL iconutil)
	find_program(MAGICK NAMES magick convert)
	if(ICONUTIL AND MAGICK)
		set(ICON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Starlane.png")
		set(ICONSET_DIR ${CMAKE_CURRENT_BINARY_DIR}/AppIcon.iconset)
		set(ICNS_OUT ${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns)
		add_custom_command(
				OUTPUT ${ICNS_OUT}
				COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONSET_DIR}
				COMMAND ${MAGICK} ${ICON_SRC} -resize 16x16     ${ICONSET_DIR}/icon_16x16.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 32x32     ${ICONSET_DIR}/icon_16x16@2x.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 32x32     ${ICONSET_DIR}/icon_32x32.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 64x64     ${ICONSET_DIR}/icon_32x32@2x.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 128x128   ${ICONSET_DIR}/icon_128x128.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 256x256   ${ICONSET_DIR}/icon_128x128@2x.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 256x256   ${ICONSET_DIR}/icon_256x256.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 512x512   ${ICONSET_DIR}/icon_256x256@2x.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 512x512   ${ICONSET_DIR}/icon_512x512.png
				COMMAND ${MAGICK} ${ICON_SRC} -resize 1024x1024 ${ICONSET_DIR}/icon_512x512@2x.png
				COMMAND ${ICONUTIL} -c icns ${ICONSET_DIR} -o ${ICNS_OUT}
				DEPENDS ${ICON_SRC}
				COMMENT "Generating AppIcon.icns from source PNG"
		)
	endif()
endif()
# Needed on macOS for macdeployqt (and to get a real .app instead of a bare
# Mach-O executable): it only knows how to deploy into a bundle. Ignored on
# other platforms.
add_executable(starlane WIN32 MACOSX_BUNDLE "starlane.cpp" mainwindow.cpp mainwindow.h outputformatter.cpp outputformatter.h blorbfile.cpp blorbfile.h debuglogwindow.cpp debuglogwindow.h)
target_link_libraries(starlane Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia starlane-core)
# ../starlane-core: the core's public API. ${CMAKE_BINARY_DIR}/generated:
# starlane-version.h (see the top-level CMakeLists.txt), for a future About
# dialog to report the running version from.
target_include_directories(starlane PRIVATE "../starlane-core" "${CMAKE_BINARY_DIR}/generated")

if(APPLE)
	# Registers the .taf extension (see packaging/Info.plist.in) so Finder can hand game files to
	# us via "open with"/double-click/drag-onto-dock-icon, delivered as a QFileOpenEvent.
	set_target_properties(starlane PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in")
	if(ICNS_OUT)
		target_sources(starlane PUBLIC ${ICNS_OUT})
		set_target_properties(starlane PROPERTIES MACOSX_BUNDLE_ICON_FILE "AppIcon")
		set_source_files_properties(${ICNS_OUT} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
	endif()
	set_target_properties(starlane PROPERTIES OUTPUT_NAME "Starlane" MACOSX_BUNDLE_NAME "Starlane")
endif()
if(WIN32)
	target_sources(starlane PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/packaging/winicon.rc")
endif()

install(TARGETS starlane
	RUNTIME DESTINATION bin
	BUNDLE DESTINATION .
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	install(FILES packaging/starlane.desktop DESTINATION share/applications)
	# packaging/icons/hicolor/512x512/apps/starlane.png -- matches
	# starlane.desktop's Icon=starlane so desktop environments (app grids,
	# taskbars, Discover/Software Center...) can look it up by theme name.
	install(DIRECTORY packaging/icons/hicolor DESTINATION share/icons)
endif()
