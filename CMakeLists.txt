# CMakeList.txt: CMake-Projekt für "starlane". Schließen Sie die Quelle ein, und definieren Sie
# projektspezifische Logik hier.
#
cmake_minimum_required (VERSION 3.8)

project("starlane")

option(SL_BUILD_QT "Build the Starlane Qt frontent" ON)

add_subdirectory("starlane-core")

if(SL_BUILD_QT)
	add_subdirectory("starlane-qt")
endif()
