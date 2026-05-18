cmake_minimum_required(VERSION 3.10)
project(BTL_MMT C)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_compile_options(-g -O0 -Wall)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Các folder ngang hàng
add_subdirectory(protocol)
add_subdirectory(utils)
add_subdirectory(client)
add_subdirectory(server)