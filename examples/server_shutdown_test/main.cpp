#include "iocpnet/iocpnet.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

std::atomic<bool> g_running{true};
std::atomic<bool> g_force_close{false};

void signal_handler(int signal) {
  std::cout << "\nReceived signal: " << signal << std::endl;
  if (g_force_close) {
    std::cout << "Force close already requested, ignoring..." << std::endl;
    return;
  }

  // Ctrl+C first time: graceful shutdown
  // Ctrl+C second time: force close
  static int count = 0;
  if (++count == 1) {
    std::cout << "Graceful shutdown requested (Ctrl+C again for force close)..." << std::endl;
    g_running = false;
  } else {
    std::cout << "Force close requested!" << std::endl;
    g_force_close = true;
    g_running = false;
  }
}

int main() {
  std::cout << "=== Server Shutdown Test ===" << std::endl;
  std::cout << "Press Ctrl+C once for graceful shutdown, twice for force close" << std::endl;

  std::signal(SIGINT, signal_handler);

  if (!iocpnet::SockExFunc::init()) {
    std::cerr << "Failed to init Winsock" << std::endl;
    return 1;
  }

  iocpnet::IOCPServer server("127.0.0.1", 9999);
  server.set_shutdown_timeout(5000);  // 5 seconds for graceful shutdown

  server.set_conn_user_callback([](iocpnet::ConnPtr conn) {
    std::cout << "[Server] New connection: " << conn->native_handle() << std::endl;
    conn->set_conn_user_callbacks(
      [conn](iocpnet::Buffer* buffer) {
        std::string msg(buffer->to_read(), buffer->readable_size());
        std::cout << "[Server] Received: " << msg;
        conn->send(buffer->to_read(), buffer->readable_size());
        buffer->been_read_all();
      },
      [conn](DWORD err) {
        std::cout << "[Server] Connection closed: " << conn->native_handle()
                  << ", err=" << err << std::endl;
      });
  });

  server.set_error_user_callback([](DWORD err) {
    std::cout << "[Server] Error: " << err << std::endl;
  });

  if (!server.start()) {
    std::cerr << "Failed to start server" << std::endl;
    iocpnet::SockExFunc::cleanup();
    return 1;
  }

  std::cout << "[Server] Started on 127.0.0.1:9999" << std::endl;

  // Run server in a separate thread
  std::thread server_thread([&server]() {
    while (g_running) {
      server.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  // Wait for shutdown signal
  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "\n=== Shutting down ===" << std::endl;
  std::cout << "Active connections: " << server.connection_count() << std::endl;

  if (g_force_close) {
    std::cout << "Calling close() for immediate shutdown..." << std::endl;
    server.close();
  } else {
    std::cout << "Calling shutdown() for graceful shutdown..." << std::endl;
    server.shutdown();
  }

  server_thread.join();

  std::cout << "\n=== Server stopped ===" << std::endl;
  std::cout << "Active connections: " << server.connection_count() << std::endl;

  iocpnet::SockExFunc::cleanup();
  return 0;
}
