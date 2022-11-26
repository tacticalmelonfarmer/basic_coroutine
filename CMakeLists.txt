cmake_minimum_required (VERSION 3.12)

project ("basic_coroutine")

add_library(basic_coroutine INTERFACE)
target_include_directories(basic_coroutine INTERFACE "include")
target_compile_features(basic_coroutine INTERFACE cxx_std_20)

add_subdirectory("examples" "examples")