cmake_minimum_required(VERSION 3.20)
project(sr-taskflow LANGUAGES CXX)

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/stdexec)
    message(FATAL_ERROR "Clone https://github.com/NVIDIA/stdexec in the root of this repo first")
endif()

add_executable(sr-taskflow sr-taskflow.cpp)
target_include_directories(sr-taskflow PUBLIC stdexec/include)
