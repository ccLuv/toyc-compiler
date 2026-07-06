cmake_minimum_required(VERSION 3.16)
project(toyc_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(compiler src/main.cpp)
if(MSVC)
  target_compile_options(compiler PRIVATE /W4 /permissive-)
else()
  target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
