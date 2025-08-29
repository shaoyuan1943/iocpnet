# Echo Server Example CMakeLists.txt

add_executable(echo_server
    main.cpp
)

# 链接核心库
target_link_libraries(echo_server
    iocpnet
)