cmake_minimum_required (VERSION 3.16)

project("slc-capi" LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)

add_library(slc-capi
        slc-capi.cpp slc-capi.h)
target_include_directories(slc-capi PRIVATE ../starlane-core)
target_link_libraries(slc-capi starlane-core)