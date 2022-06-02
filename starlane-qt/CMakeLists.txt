cmake_minimum_required(VERSION 3.8)

project(starlane_qt CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 COMPONENTS Core Gui Widgets CONFIG REQUIRED)
set(CMAKE_AUTOMOC ON)

add_executable(starlane "starlane.cpp" )
target_link_libraries(starlane Qt6::Core Qt6::Gui Qt6::Widgets starlane-core)
target_include_directories(starlane PRIVATE "../starlane-core")