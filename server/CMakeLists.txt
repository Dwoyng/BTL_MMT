# Tạo file thực thi rdt_server
add_executable(rdt_server server.c)

# Link tới các thư viện tầng dưới
target_link_libraries(rdt_server PUBLIC utils rdt_protocol pthread)

# Cho phép server tìm thấy các header ở thư mục gốc
target_include_directories(rdt_server PUBLIC ${CMAKE_SOURCE_DIR})