#include "iocp_server.h"
#include "iocp_acceptor.h"
#include "iocp_conn.h"
#include "iocp_event_poll.h"

#include <format>

namespace iocpnet {
  IOCPServer::IOCPServer(std::string_view addr, uint16_t port, ProtocolStack proto_stack, int option)
      : iocp_handle_ {INVALID_HANDLE_VALUE}
      , proto_stack_ {proto_stack}
      , sock_option_ {option}
      , started_ {false}
      , running_mode_ {RunningMode::kAllOneThread}
      , thread_num_ {0}
      , listen_addr_ {get_sockaddr(addr.data(), port, proto_stack)} {
  }

  IOCPServer::~IOCPServer() {
    shutdown();
  }

  void IOCPServer::shutdown() {
    if (!started_) { return; }

    if (acceptor_) { acceptor_->shutdown(); }

    if (running_mode_ == RunningMode::kOnePollPerThread) {
      for (const auto& p : sub_polls_) {
        if (p->joinable()) { p->join(); }
      }
    }

    if (main_poll_) { main_poll_->shutdown(); }

    if (iocp_handle_ != NULL && iocp_handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(iocp_handle_);
      iocp_handle_ = INVALID_HANDLE_VALUE;
    }

    started_ = false;
  }

  bool IOCPServer::start(RunningMode mode, int n) {
    if (mode == RunningMode::kOnePollPerThread) {
      if (n <= 0) { return false; }
    }

    if (mode == RunningMode::kAllOneThread) {
      if (n != 0) { return false; }
    }

    running_mode_ = mode;
    thread_num_   = n;
    iocp_handle_  = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocp_handle_ == INVALID_HANDLE_VALUE) { return false; }

    main_poll_ = std::make_unique<IOCPEventPoll>(iocp_handle_);
    main_poll_->set_name("main_poll");

    acceptor_ = std::make_shared<IOCPAcceptor>(main_poll_.get());
    if (!acceptor_->start(6, listen_addr_, proto_stack_, sock_option_)) {
      acceptor_->shutdown();
      acceptor_.reset();
      return false;
    }

    acceptor_->set_conn_callback(std::bind(&IOCPServer::_on_new_connection, this, std::placeholders::_1, std::placeholders::_2));
    acceptor_->set_error_callback(std::bind(&IOCPServer::_on_accept_error, this, std::placeholders::_1));

    if (running_mode_ == RunningMode::kOnePollPerThread) {
      sub_polls_.resize(thread_num_);
      for (auto i = 0; i < thread_num_; i++) {
        auto poll = std::make_unique<IOCPEventPoll>(iocp_handle_);
        poll->set_name(std::format("sub_poll_{}", i + 1));
        sub_polls_.push_back(std::make_unique<std::thread>([p = std::move(poll)]() {
          p->run();
        }));
      }
    }

    started_ = true;
    return started_;
  }

  void IOCPServer::_on_new_connection(socket_t handle, sockaddr_storage remote_addr) {
    char     client_ip_str[INET6_ADDRSTRLEN] = {0};
    uint16_t client_port                     = 0;

    if (remote_addr.ss_family == AF_INET) {
      sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&remote_addr);
      inet_ntop(AF_INET, &sin->sin_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin->sin_port);
    }

    if (remote_addr.ss_family == AF_INET6) {
      sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&remote_addr);
      inet_ntop(AF_INET6, &sin6->sin6_addr, client_ip_str, sizeof(client_ip_str));
      client_port = ntohs(sin6->sin6_port);
    }

    if (client_port == 0 || strlen(client_ip_str) == 0) { return; }

    auto conn = std::make_shared<IOCPConn>(handle);

    {
      std::lock_guard<std::mutex> locker(mutex_);
      conns_[handle] = conn;
    }

    conn->_start();

    if (on_conn_func_ != nullptr) { on_conn_func_(conn); }
  }

  void IOCPServer::poll() {
    if (!started_) { return; }
    main_poll_->poll();
  }

  void IOCPServer::run() {
    if (!started_) { return; }
    main_poll_->run();
  }

  void IOCPServer::_on_accept_error(DWORD err) {
    if (err != ERROR_OPERATION_ABORTED) {
      if (on_err_func_ != nullptr) { on_err_func_(err); }
    }
  }
} // namespace iocpnet