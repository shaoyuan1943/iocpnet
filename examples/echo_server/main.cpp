#include "iocpnet/iocpnet.h"

#include <iostream>

int main() {
  if (!iocpnet::SockExFunc::init()) {
    std::cerr << "Failed to initialize Winsock or load extensions" << std::endl;
    return 1;
  }

  iocpnet::IOCPServer server("127.0.0.1", 9899);
  server.set_conn_user_callback([](iocpnet::ConnPtr conn) {
    std::cout << "new conn" << conn->native_handle() << std::endl;
    conn->set_conn_user_callbacks(
      [conn](iocpnet::Buffer* buffer) {
        conn->send(buffer->to_read(), buffer->readable_size());
        buffer->been_read_all();
      },
      [conn](DWORD err) {
        std::cout << "conn closed: " << err << ", state: " << conn->state_string() << std::endl;
      });
  });

  server.set_error_user_callback([](DWORD err) {
    std::cout << "server error: " << err << std::endl;
  });

  if (!server.start()) {
    std::cerr << "start error" << std::endl;
    iocpnet::SockExFunc::cleanup();
    return 1;
  }

  std::cout << "server start running" << std::endl;
  server.run();

  iocpnet::SockExFunc::cleanup();
  return 0;
}
