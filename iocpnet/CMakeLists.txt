# IOCP Network Library CMakeLists.txt

# Define the library
add_library(iocpnet
        # Core source files
        iocp_channel.cc
        iocp_event_poll.cc
        iocp_server.cc
        iocp_sockex_func.cc
        iocp_sock.cc
        iocp_acceptor.h
        iocp_acceptor.cc
        iocp_conn.h
        iocp_conn.cc
        iocp_connector.h
        iocp_connector.cc
        iocpnet.h
)

# Set include directories - this allows #include <iocpnet/xxx.h> to work
target_include_directories(iocpnet PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..  # This points to the parent directory
)

# Link required Windows libraries
if (WIN32)
    target_link_libraries(iocpnet
            ws2_32  # Windows Sockets 2
            Mswsock
    )
endif ()