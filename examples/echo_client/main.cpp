#include "iocpnet/iocpnet.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// 全局变量
static std::atomic<bool>                       should_exit(false);
// T5 修复：使用 atomic 保护 g_conn 的多线程访问
static std::atomic<std::shared_ptr<iocpnet::IOCPConn>> g_conn(nullptr);

int main() {
  if (!iocpnet::SockExFunc::init()) {
    std::cerr << "Failed to initialize Winsock or load extensions" << std::endl;
    return 1;
  }

  // 创建IOCP句柄
  HANDLE iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (iocp_handle == INVALID_HANDLE_VALUE) {
    std::cerr << "Failed to create IOCP handle" << std::endl;
    WSACleanup();
    return 1;
  }

  // 创建IOCP事件循环
  iocpnet::IOCPEventPoll poll(iocp_handle);
  
  // 创建连接对象
  auto conn = std::make_shared<iocpnet::IOCPConn>(&poll);
  g_conn.store(conn, std::memory_order_release);

  // 设置回调
  conn->set_conn_user_callbacks(
    [](iocpnet::Buffer* buffer) {
      // 接收到服务器消息
      std::string msg(buffer->to_read(), buffer->readable_size());
      std::cout << "Received from server: " << msg;
      buffer->been_read_all();
    },

    [](DWORD err) {
      std::cout << "Connection closed/error: " << err << std::endl;
      should_exit = true;
    });

  // 启动异步连接
  if (!conn->connect("127.0.0.1", 9899)) {
    std::cerr << "Failed to initiate connection" << std::endl;
    WSACleanup();
    return 1;
  }

  std::cout << "Connecting to server..." << std::endl;

  // 等待连接建立（最多等待 5 秒）
  int wait_count = 0;
  while (!conn->connected() && wait_count < 500) {
    poll.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    wait_count++;
  }

  if (!conn->connected()) {
    std::cerr << "Connection timeout" << std::endl;
    WSACleanup();
    return 1;
  }

  std::cout << "Connected to server!" << std::endl;

  // 启动一个线程读取控制台输入
  std::thread input_thread([]() {
    std::string line;
    while (!should_exit && std::getline(std::cin, line)) {
      auto c = g_conn.load(std::memory_order_acquire);
      if (!line.empty() && c && c->connected()) {
        line += "\n"; // 添加换行符
        c->send(line.c_str(), line.size());
      }
    }
    // 注意：cin 结束不立即退出，等待服务器响应
    // should_exit 留给连接关闭回调设置
  });
  input_thread.detach();

  // 运行事件循环
  while (!should_exit) {
    poll.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  iocpnet::SockExFunc::cleanup();
  return 0;
}
    