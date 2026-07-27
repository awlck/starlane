cmake_minimum_required(VERSION 3.10)

project(miniz C)


add_library(miniz STATIC "miniz.c" "miniz.h")
target_compile_definitions(miniz PUBLIC MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
if (UNIX)
    target_compile_options(miniz PRIVATE "-Wno-unused-function")
endif()