cmake_minimum_required(VERSION 3.10)
project(main)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(main main.cpp)

target_compile_options(main PRIVATE
    -Wall
    -Wextra
    -fdiagnostics-color=always
)

# Debug (ASan)
target_compile_options(main PRIVATE
    $<$<CONFIG:Debug>:-O0 -g -fsanitize=address -fno-omit-frame-pointer>
)

target_link_options(main PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)

target_compile_options(main PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native>
)