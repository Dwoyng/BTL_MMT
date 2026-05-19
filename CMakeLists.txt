cmake_minimum_required(VERSION 3.10)
project(utils_project)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PROJECT_SOURCES
    protocol/rdt.c
    utils/go_back_end.c
    utils/selective_repeat.c
    utils/stop_n_wait.c
)

add_executable(rdt_client client/client.c ${PROJECT_SOURCES})
add_executable(rdt_server server/server.c ${PROJECT_SOURCES})

# 3. Định nghĩa đường dẫn include CHUẨN XÁC (Chỉ trỏ vào THƯ MỤC, không trỏ vào file .c)
target_include_directories(rdt_client PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/protocol
    ${CMAKE_SOURCE_DIR}/utils
)

target_include_directories(rdt_server PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/protocol
    ${CMAKE_SOURCE_DIR}/utils
)

find_package(Threads REQUIRED)
target_link_libraries(rdt_client PRIVATE Threads::Threads)
target_link_libraries(rdt_server PRIVATE Threads::Threads)