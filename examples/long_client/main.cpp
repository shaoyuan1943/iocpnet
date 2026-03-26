#include "iocpnet/iocpnet.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<bool> g_connected{false};
std::atomic<bool> g_closed{false};

int main() {
  std::cout << "=== Long-running Client ===" << std::endl;

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
        g_closed = true;
      });

    std::cout << "[Client] Connecting to 127.0.0.1:9999..." << std::endl;
    if (!conn->connect("127.0.0.1", 9999)) {
      std::cerr << "[Client] Failed to connect" << std::endl;
      conn.reset();
      goto cleanup;
    }

    for (int i = 0; i < 100 && !conn->connected(); ++i) {
      poll.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!conn->connected()) {
      std::cerr << "[Client] Connection timeout" << std::endl;
      conn.reset();
      goto cleanup;
    }

    std::cout << "[Client] Connected! Waiting for server to close..." << std::endl;
    g_connected = true;

    conn->send("Hello\n", 6);

    // Wait for server to close connection
    for (int i = 0; i < 300 && !g_closed; ++i) {
      poll.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (g_closed) {
      std::cout << "[Client] Server closed connection gracefully" << std::endl;
    } else {
      std::cout << "[Client] Timeout waiting for close" << std::endl;
    }

    conn.reset();
cleanup:
    ;
  }

  CloseHandle(iocp);
  iocpnet::SockExFunc::cleanup();
  return 0;
}
