# IOCP Network Library CMakeLists.txt

# Define the library
add_library(iocpnet
        # Core source files
        iocp_channel.cc
        iocp_event_poll.cc
        iocp_server.cc
        iocp_sockex_func.cc
        iocp_sock.cc
        iocp_timer.cc
        iocp_acceptor.h
        iocp_acceptor.cc
        iocp_conn.h
        iocp_conn.cc
        iocpnet.h
)

# Set include directories - this allows #include <iocpnet/xxx.h> to work
target_include_directories(iocpnet PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # This points to the parent directory
)

# Define a variable with all the source files for direct inclusion
set(IOCPNET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_channel.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_event_poll.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_server.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_sockex_func.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_sock.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_timer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_acceptor.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iocp_conn.cc
    PARENT_SCOPE
)