#include "iocp_connector.h"
#include "iocp_channel.h"
#include "iocp_event_poll.h"
#include "iocp_sockex_func.h"

namespace iocpnet {
  IOCPConnector::IOCPConnector(IOCPEventPoll* poll, std::string_view addr, uint16_t port)
      : event_poll_ {poll}
      , iocp_handle_ {INVALID_HANDLE_VALUE}
      , addr_ {addr}
      , port_ {port}
      , addr_storage_ {}
      , handle_ {invalid_socket}
      , connect_context_ {nullptr}
      , channel_ {nullptr} {
  }

  IOCPConnector::~IOCPConnector() {
  }

  void IOCPConnector::shutdown() {
  }

  bool IOCPConnector::start() {
    if (handle_ != invalid_socket) { return false; }

    IPType        ip_type        = ip_address_type(addr_.c_str());
    ProtocolStack protocol_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) { return false; }

    addr_storage_ = get_sockaddr(addr_.c_str(), port_, protocol_stack);
    if (addr_storage_.ss_family == 0) { return false; }

    handle_ = WSASocketW(addr_storage_.ss_family, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (handle_ == invalid_socket) { return false; }

    socklen_t addr_len = 0;
    switch (addr_storage_.ss_family) {
    case AF_INET:
      addr_len = sizeof(sockaddr_in);
      break;
    case AF_INET6:
      addr_len = sizeof(sockaddr_in6);
      break;
    }

    if (bind(handle_, reinterpret_cast<sockaddr*>(&addr_storage_), addr_len) == SOCKET_ERROR) {
      closesocket(handle_);
      handle_ = invalid_socket;
      return false;
    }

    if (!event_poll_->register_in(handle_, nullptr)) {
      closesocket(handle_);
      handle_ = invalid_socket;
      return false;
    }

    channel_ = std::make_unique<IOCPChannel>(handle_);
    channel_->set_connect_callback(std::bind(&IOCPConnector::_on_completion_connect, shared_from_this(), std::placeholders::_1));
    channel_->set_error_callback(std::bind(&IOCPConnector::_on_completion_error, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
    channel_->tie(shared_from_this());

    connect_context_          = new ConnectContext;
    connect_context_->channel = channel_.get();

    LPFN_CONNECTEX connectex_func = SockExFunc::load_connectex_func(handle_);
    if (connectex_func == nullptr) {
      closesocket(handle_);
      handle_ = invalid_socket;
      return false;
    }

    DWORD sent   = 0;
    BOOL  result = connectex_func(handle_, reinterpret_cast<sockaddr*>(&addr_storage_), addr_len,
                                  NULL, 0, &sent, &connect_context_->ol);
    if (!result) {
      if (DWORD err = WSAGetLastError(); err != WSA_IO_PENDING) {
        closesocket(handle_);
        delete connect_context_;
        connect_context_ = nullptr;
      }
    }

    channel_->incr_io_count();
    return true;
  }

  void IOCPConnector::_on_completion_error(OverlappedContext* context, DWORD err) {
    std::unique_ptr<ConnectContext> ptr(static_cast<ConnectContext*>(context));
    channel_->decr_io_count();
    if (err == ERROR_OPERATION_ABORTED) {
      if (channel_->pending_io_count() <= 0) {
        shutdown();
      }
    }

    if (on_err_func_ != nullptr) { on_err_func_(err); }
  }

  void IOCPConnector::_on_completion_connect(ConnectContext* context) {
    channel_->decr_io_count();

    std::unique_ptr<ConnectContext> ptr(context);
    int                             result = setsockopt(handle_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
    if (result != 0) {
      closesocket(handle_);
      handle_ = invalid_socket;
      return;
    }

    if (on_conn_func_ != nullptr) { on_conn_func_(handle_, addr_storage_); }
  }
} // namespace iocpnet
