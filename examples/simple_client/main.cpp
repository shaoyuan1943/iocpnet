#include "iocpnet/iocpnet.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  std::cout << "=== Simple Client ===" << std::endl;

  if (!iocpnet::SockExFunc::init()) {
    std::cerr << "Failed to init Winsock" << std::endl;
    return 1;
  }

  HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (iocp == INVALID_HANDLE_VALUE) {
    std::cerr << "Failed to create IOCP" << std::endl;
    iocpnet::SockExFunc::cleanup();
    return 1;
  }

  {
    iocpnet::IOCPEventPoll poll(iocp);
    auto conn = std::make_shared<iocpnet::IOCPConn>(&poll);

    conn->set_conn_user_callbacks(
      [](iocpnet::Buffer* buffer) {
        std::string msg(buffer->to_read(), buffer->readable_size());
        std::cout << "[Client] Received: " << msg;
        buffer->been_read_all();
      },
      [](DWORD err) {
        std::cout << "[Client] Closed, err=" << err << std::endl;
      });

    std::cout << "[Client] Connecting to 127.0.0.1:9999..." << std::endl;

    if (!conn->connect("127.0.0.1", 9999)) {
      std::cerr << "[Client] Failed to connect" << std::endl;
    } else {
      for (int i = 0; i < 100 && !conn->connected(); ++i) {
        poll.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      if (conn->connected()) {
        std::cout << "[Client] Connected!" << std::endl;
        conn->send("Hello Server\n", 13);

        for (int i = 0; i < 10; ++i) {
          poll.poll();
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      } else {
        std::cout << "[Client] Connection timeout" << std::endl;
      }
    }

    conn.reset();
  }

  CloseHandle(iocp);
  iocpnet::SockExFunc::cleanup();
  return 0;
}