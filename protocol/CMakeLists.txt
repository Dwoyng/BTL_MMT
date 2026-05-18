cmake_minimum_required(VERSION 3.10)
project(protocol_project)

set(PROTOCOL_SOURCE
    rdt.c
)

add_library(rdt_protocol STATIC rdt.c)

target_include_directories(rdt_protocol PUBLIC  ${PROTOCOL_SOURCE})
