cmake_minimum_required(VERSION 3.15)
project(ssg CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ssg
    src/main.cpp
    src/markdown.cpp
    src/build_cache.cpp
)
target_include_directories(ssg PRIVATE include)
