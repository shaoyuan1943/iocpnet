#include "iocp_acceptor.h"
#include "iocp_channel.h"
#include "iocp_conn.h"
#include "iocp_context.h"
#include "iocp_event_poll.h"
#include "iocp_sockex_func.h"

namespace iocpnet {
  IOCPAcceptor::IOCPAcceptor(IOCPEventPoll* poll)
      : event_poll_ {poll}
      , iocp_handle_ {INVALID_HANDLE_VALUE}
      , listen_handle_ {invalid_socket}
      , listen_sockaddr_ {} {
  }

  IOCPAcceptor::~IOCPAcceptor() {
    shutdown();
  }

  void IOCPAcceptor::shutdown() {
    if (listen_handle_ != invalid_socket) {
      closesocket(listen_handle_);
      listen_handle_   = invalid_socket;
      listen_sockaddr_ = {};
    }
  }

  bool IOCPAcceptor::start(int accept_n, sockaddr_storage addr, ProtocolStack protocol_stack, int option) {
    if (listening()) { return true; }

    listen_sockaddr_ = addr;
    if (listen_sockaddr_.ss_family == 0) { return false; }

    listen_handle_ = WSASocketW(listen_sockaddr_.ss_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listen_handle_ == invalid_socket) { return false; }

    if (protocol_stack == ProtocolStack::kDualStack && listen_sockaddr_.ss_family == AF_INET6) {
      DWORD zero = 0;
      setsockopt(listen_handle_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&zero), sizeof(zero));
    }

    if ((option & SocketOption::kReuseAddr) == SocketOption::kReuseAddr) {
      int reuse_addr = 1;
      if (setsockopt(listen_handle_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse_addr), sizeof(reuse_addr)) == SOCKET_ERROR) {
        closesocket(listen_handle_);
        return false;
      }
    }

    if (!set_non_blocking(listen_handle_)) {
      closesocket(listen_handle_);
      return false;
    }

    socklen_t addr_len = 0;
    switch (listen_sockaddr_.ss_family) {
    case AF_INET:
      addr_len = sizeof(sockaddr_in);
      break;
    case AF_INET6:
      addr_len = sizeof(sockaddr_in6);
      break;
    }

    if (bind(listen_handle_, reinterpret_cast<sockaddr*>(&listen_sockaddr_), addr_len) == SOCKET_ERROR) {
      closesocket(listen_handle_);
      return false;
    }

    if (listen(listen_handle_, SOMAXCONN) == SOCKET_ERROR) {
      closesocket(listen_handle_);
      return false;
    }
    if (!event_poll_->register_in(listen_handle_, nullptr)) {
      closesocket(listen_handle_);
      return false;
    }

    channel_ = std::make_unique<IOCPChannel>(listen_handle_);
    channel_->set_accept_callback(std::bind(&IOCPAcceptor::_on_completion_accept, this, std::placeholders::_1, std::placeholders::_2));
    channel_->set_error_callback(std::bind(&IOCPAcceptor::_on_completion_error, this, std::placeholders::_1, std::placeholders::_2));
    channel_->tie(shared_from_this());

    if (accept_n <= 0) { accept_n = 1; }

    for (int i = 0; i < accept_n; i++) {
      _go_accepting();
    }

    return true;
  }

  void IOCPAcceptor::_go_accepting() {
    if (!listening()) { return; }

    LPFN_ACCEPTEX acceptex_func = SockExFunc::load_acceptex_func(listen_handle_);
    if (acceptex_func == nullptr) {
      closesocket(listen_handle_);
      return;
    }

    AcceptContext* context = new AcceptContext(listen_handle_);
    context->accept_handle = WSASocketW(listen_sockaddr_.ss_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    context->channel       = channel_.get();
    if (!event_poll_->register_in(context->accept_handle, context)) {
      closesocket(context->accept_handle);
      delete context;
      return;
    }

    BOOL result = acceptex_func(listen_handle_, context->accept_handle,
                                context->sockaddr_buffer,
                                0,
                                sizeof(sockaddr_storage) + 16,
                                sizeof(sockaddr_storage) + 16,
                                nullptr,
                                &context->ol);
    if (!result && GetLastError() != ERROR_IO_PENDING) {
      closesocket(context->accept_handle);
      channel_.reset();
      delete context;
    }

    channel_->incr_io_count();
  }

  void IOCPAcceptor::_on_completion_accept(AcceptContext* context, DWORD bytes_transferred) {
    std::unique_ptr<AcceptContext> ptr(static_cast<AcceptContext*>(context));
    channel_->decr_io_count();
    if (setsockopt(context->accept_handle, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   reinterpret_cast<char*>(&context->listen_handle), sizeof(context->listen_handle)) == SOCKET_ERROR) {
      closesocket(context->accept_handle);
      _go_accepting();
      return;
    }

    sockaddr_storage* local_addr      = nullptr;
    int               local_addr_len  = 0;
    sockaddr_storage* remote_addr     = nullptr;
    int               remote_addr_len = 0;

    GetAcceptExSockaddrs(context->sockaddr_buffer,
                         bytes_transferred,
                         sizeof(sockaddr_storage) + 16,
                         sizeof(sockaddr_storage) + 16,
                         (LPSOCKADDR*)&local_addr,
                         &local_addr_len,
                         (LPSOCKADDR*)&remote_addr,
                         &remote_addr_len);
    if (on_conn_func_ != nullptr) { on_conn_func_(context->accept_handle, *remote_addr); }

    _go_accepting();
  }

  void IOCPAcceptor::_on_completion_error(OverlappedContext* context, DWORD err) {
    std::unique_ptr<AcceptContext> ptr(static_cast<AcceptContext*>(context));
    channel_->decr_io_count();
    if (err == ERROR_OPERATION_ABORTED) {
      if (channel_->pending_io_count() <= 0) {
        shutdown();
      }
    }

    if (on_err_func_ != nullptr) { on_err_func_(err); }
  }

} // namespace iocpnet