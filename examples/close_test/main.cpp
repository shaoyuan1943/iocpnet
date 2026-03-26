#include "iocpnet/iocpnet.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

std::atomic<bool> g_connected{false};
std::atomic<bool> g_closed{false};
std::atomic<DWORD> g_close_err{0xFFFFFFFF};

int main() {
  std::cout << "=== Close Flow Test ===" << std::endl;

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

  int result = 1;

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
        g_close_err = err;
      });

    conn->set_close_timeout(5000);

    std::cout << "[Client] Connecting to 127.0.0.1:9899..." << std::endl;

    if (!conn->connect("127.0.0.1", 9899)) {
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

    std::cout << "[Client] Connected!" << std::endl;
    g_connected = true;

    conn->send("Hello\n", 6);

    for (int i = 0; i < 20; ++i) {
      poll.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "[Client] Calling shutdown()..." << std::endl;
    conn->shutdown();

    for (int i = 0; i < 100 && !g_closed; ++i) {
      poll.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "[Client] Final state: " << conn->state_string() << std::endl;

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Connected: " << (g_connected ? "YES" : "NO") << std::endl;
    std::cout << "Closed: " << (g_closed ? "YES" : "NO") << std::endl;

    if (g_connected && g_closed && g_close_err == 0) {
      std::cout << "\n=== TEST PASSED ===" << std::endl;
      result = 0;
    } else {
      std::cout << "\n=== TEST FAILED ===" << std::endl;
    }

    std::cout.flush();
    std::cerr.flush();

    conn.reset();

cleanup:
    ;
  }

  CloseHandle(iocp);
  iocpnet::SockExFunc::cleanup();

  return result;
}