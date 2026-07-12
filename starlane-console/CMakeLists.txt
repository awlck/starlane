cmake_minimum_required(VERSION 3.10)

project(starlane_console CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(starlane-console "console.cpp")
target_link_libraries(starlane-console starlane-core)
target_include_directories(starlane-console PRIVATE ../starlane-core)
