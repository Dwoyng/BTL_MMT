# Tạo file thực thi rdt_client
add_executable(rdt_client client.c)

# Link tới các thư viện tầng dưới và thư viện luồng
target_link_libraries(rdt_client PUBLIC utils rdt_protocol pthread)

# Cho phép client tìm thấy các header ở thư mục gốc
target_include_directories(rdt_client PUBLIC ${CMAKE_SOURCE_DIR})