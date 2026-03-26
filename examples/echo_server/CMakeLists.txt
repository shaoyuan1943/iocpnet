# Echo Server Example CMakeLists.txt

set(IOCPNET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../iocpnet)
file(GLOB IOCPNET_SOURCES
    ${IOCPNET_DIR}/*.cc
)

add_executable(echo_server
    main.cpp
    ${IOCPNET_SOURCES}
)

target_include_directories(echo_server PRIVATE
    ../..
)

target_link_libraries(echo_server
    ws2_32
    mswsock
)

set_target_properties(echo_server PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
