#include "iocp_acceptor.h"
#include "iocp_channel.h"
#include "iocp_conn.h"
#include "iocp_context.h"
#include "iocp_event_poll.h"
#include "iocp_sockex_func.h"

#include <iostream>

namespace iocpnet {
  IOCPAcceptor::IOCPAcceptor(std::shared_ptr<IOCPEventPoll> poll)
      : event_poll_ {poll}
      , shut_ {false}
      , handle_ {INVALID_HANDLE_VALUE}
      , listen_handle_ {invalid_socket}
      , listen_sockaddr_ {} {}

  IOCPAcceptor::~IOCPAcceptor() { shutdown(); }

  void IOCPAcceptor::shutdown() {
    if (shut_.exchange(true)) { return; }

    if (listen_handle_ != invalid_socket) {
      closesocket(listen_handle_);
      listen_handle_ = invalid_socket;
    }
  }

  bool IOCPAcceptor::start(int accept_n, sockaddr_storage addr, ProtocolStack protocol_stack, int option) {
    if (listening()) { return true; }

    listen_sockaddr_ = addr;
    if (listen_sockaddr_.ss_family == 0) { return false; }

    listen_handle_ = WSASocketW(listen_sockaddr_.ss_family,
                                SOCK_STREAM,
                                IPPROTO_TCP,
                                nullptr,
                                0,
                                WSA_FLAG_OVERLAPPED);
    if (listen_handle_ == invalid_socket) { return false; }

    if (listen_sockaddr_.ss_family == AF_INET6) {
      if (protocol_stack == ProtocolStack::kDualStack) {
        DWORD zero = 0;
        setsockopt(listen_handle_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&zero), sizeof(zero));
      }
      else if (protocol_stack == ProtocolStack::kIPv6Only) {
        DWORD one = 1;
        setsockopt(listen_handle_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&one), sizeof(one));
      }
    }

    if ((option & SocketOption::kReuseAddr) == SocketOption::kReuseAddr) {
      int reuse_addr = 1;
      setsockopt(listen_handle_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse_addr), sizeof(reuse_addr));
    }

    if (!set_non_blocking(listen_handle_)) {
      closesocket(listen_handle_);
      return false;
    }

    socklen_t addr_len = 0;
    switch (listen_sockaddr_.ss_family) {
    case AF_INET: addr_len = sizeof(sockaddr_in); break;
    case AF_INET6: addr_len = sizeof(sockaddr_in6); break;
    }

    if (bind(listen_handle_, reinterpret_cast<sockaddr*>(&listen_sockaddr_), addr_len) == SOCKET_ERROR) {
      closesocket(listen_handle_);
      return false;
    }

    if (listen(listen_handle_, SOMAXCONN) == SOCKET_ERROR) {
      closesocket(listen_handle_);
      return false;
    }

    // 将 Channel 作为 Handler 绑定到 IOCP
    channel_ = std::make_unique<IOCPChannel>(listen_handle_);
    if (!event_poll_->register_in(listen_handle_, channel_.get())) {
      closesocket(listen_handle_);
      channel_.reset();
      return false;
    }

    auto self = shared_from_this();
    channel_->set_accept_callback([self](AcceptContext* ctx, DWORD bytes) { self->on_completion_accept_(ctx, bytes); });
    channel_->set_error_callback([self](OverlappedContext* ctx, DWORD err) { self->on_completion_error_(ctx, err); });
    channel_->tie(self);

    if (accept_n <= 0) { accept_n = 1; }
    for (int i = 0; i < accept_n; i++) { go_accepting_(); }

    return true;
  }

  void IOCPAcceptor::go_accepting_() {
    // L1 修复：使用循环代替递归，防止栈溢出
    while (listening()) {
      if (!listening()) { return; }

      AcceptContext* context = event_poll_->accept_pool().acquire(listen_handle_);
      context->accept_handle = WSASocketW(listen_sockaddr_.ss_family,
                                          SOCK_STREAM,
                                          IPPROTO_TCP,
                                          nullptr,
                                          0,
                                          WSA_FLAG_OVERLAPPED);
      if (context->accept_handle == invalid_socket) {
        event_poll_->accept_pool().release(context);
        // 创建 socket 失败，退出循环，等待下次调用
        return;
      }

      context->channel = channel_.get();
      channel_->incr_io_count();

      LPFN_ACCEPTEX acceptex_func = SockExFunc::acceptex();
      if (acceptex_func == nullptr) {
        closesocket(context->accept_handle);
        channel_->decr_io_count();
        event_poll_->accept_pool().release(context);
        return;
      }

      BOOL          result        = acceptex_func(listen_handle_,
                                                  context->accept_handle,
                                                  context->sockaddr_buffer,
                                                  0,
                                                  sizeof(sockaddr_storage) + 16,
                                                  sizeof(sockaddr_storage) + 16,
                                                  nullptr,
                                                  &context->ol);
      if (!result && GetLastError() != ERROR_IO_PENDING) {
        closesocket(context->accept_handle);
        channel_->decr_io_count();
        event_poll_->accept_pool().release(context);
        // L1 修复：不递归调用，而是返回让 IOCP 继续处理其他事件
        // 下次有新连接时会再次调用 go_accepting_
        return;
      }
      // 成功发起了 AcceptEx，退出循环
      return;
    }
  }

  void IOCPAcceptor::on_completion_accept_(AcceptContext* context, DWORD bytes_transferred) {
    channel_->decr_io_count();

    if (shut_.load()) {
      closesocket(context->accept_handle);
      event_poll_->accept_pool().release(context);
      return;
    }

    if (setsockopt(context->accept_handle,
                   SOL_SOCKET,
                   SO_UPDATE_ACCEPT_CONTEXT,
                   reinterpret_cast<char*>(&context->listen_handle),
                   sizeof(context->listen_handle)) == SOCKET_ERROR) {
      closesocket(context->accept_handle);
      event_poll_->accept_pool().release(context);
      go_accepting_();
      return;
    }

    LPFN_GETACCEPTEXSOCKADDRS getsockaddrs_func = SockExFunc::getsockaddrs();
    assert(getsockaddrs_func != nullptr);

    sockaddr_storage* local_addr      = nullptr;
    int               local_addr_len  = 0;
    sockaddr_storage* remote_addr     = nullptr;
    int               remote_addr_len = 0;

    getsockaddrs_func(context->sockaddr_buffer,
                      bytes_transferred,
                      sizeof(sockaddr_storage) + 16,
                      sizeof(sockaddr_storage) + 16,
                      (LPSOCKADDR*)&local_addr,
                      &local_addr_len,
                      (LPSOCKADDR*)&remote_addr,
                      &remote_addr_len);
    assert(remote_addr != nullptr);

    if (on_conn_func_ != nullptr) { on_conn_func_(context->accept_handle, *remote_addr); }
    event_poll_->accept_pool().release(context);
    go_accepting_();
  }

  void IOCPAcceptor::on_completion_error_(OverlappedContext* context, DWORD err) {
    AcceptContext* accept_ctx = static_cast<AcceptContext*>(context);
    channel_->decr_io_count();

    if (err == ERROR_OPERATION_ABORTED) {
      // 只有当所有挂起的 AcceptEx 都返回后，才彻底销毁 Channel 从而释放 Acceptor
      if (channel_->pending_io_count() <= 0) {
        channel_.reset();
      }
    }
    else {
      // 如果是普通错误（如客户端连接重置），重新开始
      if (listening()) {
        go_accepting_();
      }
    }

    event_poll_->accept_pool().release(accept_ctx);
    if (on_err_func_ != nullptr) { on_err_func_(err); }
  }

} // namespace iocpnet
