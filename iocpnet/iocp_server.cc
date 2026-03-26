#include "iocp_server.h"
#include "iocp_acceptor.h"
#include "iocp_conn.h"
#include "iocp_event_poll.h"

#include <chrono>
#include <format>
#include <iostream>

namespace iocpnet {
  IOCPServer::IOCPServer(std::string_view addr, uint16_t port, ProtocolStack proto_stack, int option)
      : handle_ {INVALID_HANDLE_VALUE}
      , proto_stack_ {proto_stack}
      , sock_option_ {option}
      , started_ {false}
      , running_mode_ {RunningMode::kAllOneThread}
      , thread_num_ {0}
      , listen_addr_ {get_sockaddr(addr.data(), port, proto_stack)} {
  }

  IOCPServer::~IOCPServer() {
    // 析构时使用优雅关闭
    shutdown();
  }

  // 优雅关闭：停止accept → 优雅关闭连接 → 等待完成或超时 → 释放资源
  void IOCPServer::shutdown() {
    if (!started_) { return; }

    if (acceptor_ != nullptr) { acceptor_->shutdown(); }

    std::vector<std::shared_ptr<IOCPConn>> conns;
    {
      std::lock_guard locker(mutex_);
      conns.reserve(conns_.size());
      for (const auto& [handle, conn] : conns_) {
        if (conn) { conns.push_back(conn); }
      }
    }
    for (const auto& conn : conns) {
      conn->shutdown();
    }

    if (shutdown_timeout_ms_ > 0) {
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(shutdown_timeout_ms_);
      while (std::chrono::steady_clock::now() < deadline) {
        if (connection_count() == 0) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    conns.clear();
    {
      std::lock_guard locker(mutex_);
      conns.reserve(conns_.size());
      for (const auto& [handle, conn] : conns_) {
        if (conn) { conns.push_back(conn); }
      }
    }
    for (const auto& conn : conns) {
      conn->close();
    }
    {
      std::lock_guard locker(mutex_);
      conns_.clear();
    }

    shutdown_polls_and_handle_();
  }

  // 立即关闭：立即释放所有资源
  void IOCPServer::close() {
    if (!started_) { return; }

    if (acceptor_ != nullptr) { acceptor_->shutdown(); }

    std::vector<std::shared_ptr<IOCPConn>> conns;
    {
      std::lock_guard locker(mutex_);
      conns.reserve(conns_.size());
      for (const auto& [handle, conn] : conns_) {
        if (conn) { conns.push_back(conn); }
      }
    }
    for (const auto& conn : conns) {
      conn->close();
    }
    {
      std::lock_guard locker(mutex_);
      conns_.clear();
    }

    shutdown_polls_and_handle_();
  }

  // 关闭 polls 和 IOCP handle 的通用逻辑
  void IOCPServer::shutdown_polls_and_handle_() {
    // 关闭所有子 poll
    for (const auto& p : sub_poll_objs_) {
      if (p != nullptr) { p->shutdown(); }
    }

    // 等待子线程结束
    if (running_mode_ == RunningMode::kOnePollPerThread) {
      for (const auto& p : sub_polls_) {
        if (p->joinable()) { p->join(); }
      }
    }

    // 关闭主 poll
    if (main_poll_) { main_poll_->shutdown(); }

    // 关闭 IOCP 句柄
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }

    started_ = false;
  }

  bool IOCPServer::start(RunningMode mode, int n) {
    if (mode == RunningMode::kOnePollPerThread) {
      if (n <= 0) {
        std::cerr << "start error: kOnePollPerThread mode requires n > 0" << std::endl;
        return false;
      }
    }

    if (mode == RunningMode::kAllOneThread) {
      if (n != 0) {
        std::cerr << "start error: kAllOneThread mode requires n == 0" << std::endl;
        return false;
      }
    }

    running_mode_ = mode;
    thread_num_   = n;
    handle_       = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE) {
      std::cerr << "start error: CreateIoCompletionPort failed with error " << GetLastError() << std::endl;
      return false;
    }

    main_poll_ = std::make_shared<IOCPEventPoll>(handle_);
    main_poll_->set_name("main_poll");

    acceptor_ = std::make_shared<IOCPAcceptor>(main_poll_);
    acceptor_->set_conn_callback(std::bind(&IOCPServer::on_new_connection_, this, std::placeholders::_1, std::placeholders::_2));
    acceptor_->set_error_callback(std::bind(&IOCPServer::on_accept_error_, this, std::placeholders::_1));

    if (!acceptor_->start(6, listen_addr_, proto_stack_, sock_option_)) {
      std::cerr << "start error: acceptor start failed" << std::endl;
      acceptor_->shutdown();
      acceptor_.reset();
      main_poll_.reset();
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
      return false;
    }

    if (running_mode_ == RunningMode::kOnePollPerThread) {
      for (auto i = 0; i < thread_num_; i++) {
        auto poll = std::make_shared<IOCPEventPoll>(handle_);
        poll->set_name(std::format("sub_poll_{}", i + 1));
        sub_poll_objs_.push_back(poll);
        sub_polls_.push_back(std::make_unique<std::thread>([poll]() {
          poll->run();
        }));
      }
    }

    started_ = true;
    return started_;
  }

  void IOCPServer::on_new_connection_(socket_t handle, sockaddr_storage remote_addr) {
    char     client_ip_str[INET6_ADDRSTRLEN] = {0};
    uint16_t client_port                     = 0;

    if (remote_addr.ss_family == AF_INET) {
      auto* sin = reinterpret_cast<sockaddr_in*>(&remote_addr);
      inet_ntop(AF_INET, &sin->sin_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin->sin_port);
    }

    if (remote_addr.ss_family == AF_INET6) {
      auto* sin6 = reinterpret_cast<sockaddr_in6*>(&remote_addr);
      inet_ntop(AF_INET6, &sin6->sin6_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin6->sin6_port);
    }

    if (client_port == 0 || strlen(client_ip_str) == 0) {
      closesocket(handle);
      return;
    }

    // 检查连接数限制
    {
      std::lock_guard locker(mutex_);
      if (max_connections_ > 0 && conns_.size() >= max_connections_) {
        // 已达到最大连接数，关闭新连接
        closesocket(handle);
        if (on_err_func_ != nullptr) {
          on_err_func_(ERROR_TOO_MANY_OPEN_FILES);
        }
        return;
      }
    }

    auto conn = std::make_shared<IOCPConn>(main_poll_.get(), handle);
    conn->set_remote_addr(client_ip_str, client_port);

    // 使用内部回调处理清理工作，不影响用户的 on_close_func
    conn->set_internal_close_callback_([this, handle](DWORD err) {
      std::lock_guard locker(mutex_);
      conns_.erase(handle);
    });

    {
      std::lock_guard locker(mutex_);
      conns_[handle] = conn;
    }

    if (on_conn_func_ != nullptr) { on_conn_func_(conn); }

    if (!conn->start()) {
      std::lock_guard locker(mutex_);
      conns_.erase(handle);
      return;
    }
  }

  void IOCPServer::poll() {
    if (!started_) { return; }
    main_poll_->poll();
  }

  void IOCPServer::run() {
    if (!started_) { return; }
    main_poll_->run();
  }

  void IOCPServer::on_accept_error_(DWORD err) {
    std::cout << "on_accept_error_: " << err << std::endl;
    if (err != ERROR_OPERATION_ABORTED) {
      if (on_err_func_ != nullptr) { on_err_func_(err); }
    }
  }
} // namespace iocpnet
