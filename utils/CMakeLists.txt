cmake_minimum_required(VERSION 3.10)
project(utils_project)

set (UTILS_SOURCE
    go_back_end.c
    stop_n_wait.c
    selective_repeat.c
)

add_library(utils STATIC ${UTILS_SOURCE})
target_include_directories(utils PUBLIC UTILS_SOURCE PROTOCOL_SOURCE)