cmake_minimum_required(VERSION 3.8)

project(miniz C)


add_library(miniz STATIC "miniz.c" "miniz.h")
target_compile_definitions(miniz INTERFACE MINIZ_NO_ARCHIVE_APIS)