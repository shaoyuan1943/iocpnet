# Echo Client Example CMakeLists.txt

# 获取iocpnet源文件
set(IOCPNET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../iocpnet)
file(GLOB IOCPNET_SOURCES 
    ${IOCPNET_DIR}/*.cc
)

add_executable(echo_client
        main.cpp
        ${IOCPNET_SOURCES}
)

# 设置包含目录，支持 #include "iocpnet/iocpnet.h" 这种包含方式
target_include_directories(echo_client PRIVATE
    ../..  # 这样就可以使用 #include "iocpnet/iocpnet.h"
)

# 链接Windows库
target_link_libraries(echo_client
        ws2_32
        mswsock
)

# 设置 C++20 标准
set_target_properties(echo_client PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)