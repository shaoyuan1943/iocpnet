#include "iocp_conn.h"
#include "buffer.h"
#include "iocp_channel.h"
#include "iocp_context.h"
#include "iocp_event_poll.h"
#include "iocp_sockex_func.h"

#include <iostream>
#include <mutex>

namespace iocpnet {
  IOCPConn::IOCPConn(IOCPEventPoll* poll, socket_t handle)
      : poll_ {poll}
      , handle_ {handle}
      , state_value_ {static_cast<int>(handle == invalid_socket ? State::kDisconnected : State::kConnecting)}
      , port_ {0}
      , read_pending_ {false}
      , read_ol_context_ {nullptr}
      , write_ol_context_ {nullptr}
      , write_in_progress_ {false} {
  }

  IOCPConn::~IOCPConn() {
    std::lock_guard locker(mutex_);
    for (auto* buf : send_queue_) {
      delete buf;
    }
    send_queue_.clear();

    if (write_ol_context_ && write_ol_context_->associated_buffer) {
      delete write_ol_context_->associated_buffer;
      write_ol_context_->associated_buffer = nullptr;
    }

    if (read_ol_context_) {
      poll_->read_pool().release(read_ol_context_);
      read_ol_context_ = nullptr;
    }
    if (write_ol_context_) {
      poll_->write_pool().release(write_ol_context_);
      write_ol_context_ = nullptr;
    }
  }

  bool IOCPConn::connect(std::string_view addr, uint16_t port) {
    if (get_state_() != State::kDisconnected) {
      return false;
    }

    size_t copy_len = (std::min)(addr.size(), (size_t)INET6_ADDRSTRLEN - 1);
    memcpy(addr_, addr.data(), copy_len);
    addr_[copy_len] = '\0';
    port_           = port;

    IPType        ip_type        = ip_address_type(addr_);
    ProtocolStack protocol_stack = (ip_type == IPType::kIPv4) ? ProtocolStack::kIPv4Only : ProtocolStack::kIPv6Only;
    if (ip_type == IPType::kInvalid) {
      return false;
    }

    addr_storage_ = get_sockaddr(addr_, port_, protocol_stack);
    if (addr_storage_.ss_family == 0) {
      return false;
    }

    handle_ = WSASocketW(addr_storage_.ss_family, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (handle_ == invalid_socket) {
      return false;
    }

    socklen_t addr_len = 0;
    switch (addr_storage_.ss_family) {
    case AF_INET: addr_len = sizeof(sockaddr_in); break;
    case AF_INET6: addr_len = sizeof(sockaddr_in6); break;
    }

    // ConnectEx 要求必须先 bind，这里绑定到通配地址和随机端口
    sockaddr_storage local_addr = {0};
    local_addr.ss_family        = addr_storage_.ss_family;

    if (bind(handle_, reinterpret_cast<sockaddr*>(&local_addr), addr_len) == SOCKET_ERROR) {
      closesocket(handle_);
      handle_ = invalid_socket;
      return false;
    }

    // 将 Channel 作为 Handler 绑定到 IOCP
    channel_ = std::make_unique<IOCPChannel>(handle_);
    if (!poll_->register_in(handle_, channel_.get())) {
      closesocket(handle_);
      handle_ = invalid_socket;
      channel_.reset();
      return false;
    }

    auto self = shared_from_this();
    channel_->set_connect_callback([self](ConnectContext* ctx) { self->on_completion_connect_(ctx); });
    channel_->set_error_callback([self](OverlappedContext* ctx, DWORD err) { self->on_completion_error_(ctx, err); });
    channel_->tie(self);

    LPFN_CONNECTEX connectex_func = SockExFunc::connectex();
    if (connectex_func == nullptr) {
      closesocket(handle_);
      handle_ = invalid_socket;
      channel_.reset();
      set_state_(State::kDisconnected);
      return false;
    }

    set_state_(State::kConnecting);

    ConnectContext* context = new ConnectContext();
    context->channel        = channel_.get();

    DWORD sent   = 0;
    BOOL  result = connectex_func(handle_, reinterpret_cast<sockaddr*>(&addr_storage_), addr_len, NULL, 0, &sent, &context->ol);
    if (result) {
      if (setsockopt(handle_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) != 0) {
        delete context;
        closesocket(handle_);
        handle_ = invalid_socket;
        channel_.reset();
        set_state_(State::kDisconnected);
        return false;
      }

      delete context;
      if (!start()) {
        close();
        return false;
      }
      return true;
    }

    if (WSAGetLastError() != WSA_IO_PENDING) {
      delete context;
      closesocket(handle_);
      handle_ = invalid_socket;
      channel_.reset();
      set_state_(State::kDisconnected);
      return false;
    }

    channel_->incr_io_count();
    return true;
  }

  void IOCPConn::shutdown() {
    if (get_state_() == State::kDisconnected || get_state_() == State::kDisconnecting) { return; }
    if (handle_ == invalid_socket) { return; }

    ::shutdown(handle_, SD_SEND); // 关闭写方向
    set_state_(State::kDisconnecting);

    // 启动关闭超时定时器
    if (close_timeout_ms_ > 0) {
      start_close_timeout_();
    }
  }

  void IOCPConn::close() {
    if (get_state_() == State::kDisconnected) { return; }
    cleanup_(0);
  }

  void IOCPConn::send(const char* msg, size_t size) {
    if (msg == nullptr || size == 0) { return; }

    Buffer* buf = new Buffer(msg, size);

    bool need_trigger_write = false;
    {
      std::lock_guard locker(mutex_);
      if (get_state_() != State::kConnected) {
        delete buf;
        return;
      }

      send_queue_.push_back(buf);
      need_trigger_write = send_queue_.size() == 1;
    }

    if (need_trigger_write) { go_writing_(); }
  }

  bool IOCPConn::start() {
    if (connected()) { return true; }

    // Acceptor进来的没有创建channel
    if (!channel_) {
      channel_ = std::make_unique<IOCPChannel>(handle_);
      if (!poll_->register_in(handle_, channel_.get())) {
        closesocket(handle_);
        handle_ = invalid_socket;
        channel_.reset();
        return false;
      }
    }

    read_buffer_ = std::make_unique<Buffer>();
    auto self    = shared_from_this();
    channel_->set_error_callback([self](OverlappedContext* context, DWORD err) { self->on_completion_error_(context, err); });
    channel_->set_read_callback([self](ReadContext* context, DWORD bytes_transferred) { self->on_completion_read_(context, bytes_transferred); });
    channel_->set_write_callback([self](WriteContext* context, DWORD bytes_transferred) { self->on_completion_write_(context, bytes_transferred); });
    channel_->tie(self);

    set_state_(State::kConnected);
    go_reading_();
    return true;
  }

  void IOCPConn::go_reading_() {
    bool expected = false;
    if (!read_pending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return;
    }

    read_buffer_->ensure_writable_size(1);
    read_ol_context_          = poll_->read_pool().acquire();
    read_ol_context_->wsa.buf = read_buffer_->to_write();
    read_ol_context_->wsa.len = read_buffer_->writable_size();
    read_ol_context_->channel = channel_.get();

    if (WSARecv(handle_, &read_ol_context_->wsa, 1, NULL,
                &read_ol_context_->flags, &read_ol_context_->ol, NULL) == SOCKET_ERROR) {
      if (DWORD err = WSAGetLastError(); err != WSA_IO_PENDING) {
        poll_->read_pool().release(read_ol_context_);
        read_ol_context_ = nullptr;
        read_pending_.store(false, std::memory_order_release);
        on_completion_error_(nullptr, err);
        return;
      }
    }

    channel_->incr_io_count();
  }

  void IOCPConn::go_writing_() {
    bool expected = false;
    if (!write_in_progress_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return;
    }

    Buffer* send_buf = nullptr;
    {
      std::lock_guard locker(mutex_);
      if (send_queue_.empty()) {
        write_in_progress_.store(false, std::memory_order_release);
        return;
      }
      send_buf = send_queue_.front();
      send_queue_.pop_front();
    }

    write_ol_context_                    = poll_->write_pool().acquire();
    write_ol_context_->wsa.buf           = send_buf->to_read();
    write_ol_context_->wsa.len           = send_buf->readable_size();
    write_ol_context_->associated_buffer = send_buf;
    write_ol_context_->channel           = channel_.get();

    DWORD sent   = 0;
    int   result = WSASend(handle_, &write_ol_context_->wsa, 1, &sent, 0,
                           &write_ol_context_->ol, nullptr);
    if (result == 0) {
      channel_->incr_io_count();
    }
    else {
      DWORD err = WSAGetLastError();
      if (err == WSA_IO_PENDING) {
        channel_->incr_io_count();
      }
      else {
        // 其他错误不会收到 IOCP 通知
        write_in_progress_.store(false, std::memory_order_release);
        delete write_ol_context_->associated_buffer;
        write_ol_context_->associated_buffer = nullptr;
        poll_->write_pool().release(write_ol_context_);
        write_ol_context_ = nullptr;
        on_completion_error_(nullptr, err);
      }
    }
  }

  std::string IOCPConn::state_string() const {
    switch (get_state_()) {
    case State::kDisconnected: return "Disconnected";
    case State::kConnecting: return "Connecting";
    case State::kConnected: return "Connected";
    case State::kDisconnecting: return "Disconnecting";
    default: return "Unknown";
    }
  }

  void IOCPConn::on_completion_read_(ReadContext* context, DWORD bytes_transferred) {
    channel_->decr_io_count();
    read_pending_.store(false, std::memory_order_release);
    if (read_ol_context_ == context) { read_ol_context_ = nullptr; }

    // remote point closed
    if (bytes_transferred == 0) {
      poll_->read_pool().release(context);
      cleanup_(0);
      return;
    }

    read_buffer_->been_written(bytes_transferred);
    if (on_message_func_ != nullptr) { on_message_func_(read_buffer_.get()); }
    poll_->read_pool().release(context);

    if (get_state_() == State::kDisconnecting) {
      cleanup_(0);
      return;
    }

    go_reading_();
  }

  void IOCPConn::on_completion_write_(WriteContext* context, DWORD bytes_transferred) {
    (void)bytes_transferred;
    channel_->decr_io_count();

    delete context->associated_buffer;
    context->associated_buffer = nullptr;
    if (write_ol_context_ == context) { write_ol_context_ = nullptr; }

    write_in_progress_.store(false, std::memory_order_release);
    bool has_more = false;
    {
      std::lock_guard locker(mutex_);
      has_more = !send_queue_.empty();
    }

    if (has_more) { go_writing_(); }
    poll_->write_pool().release(context);
  }

  void IOCPConn::on_completion_connect_(ConnectContext* context) {
    std::unique_ptr<ConnectContext> ptr(context);
    channel_->decr_io_count();

    if (setsockopt(handle_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) != 0) {
      cleanup_(WSAGetLastError());
      return;
    }

    if (!start()) {
      cleanup_(0);
    }
  }

  void IOCPConn::on_completion_error_(OverlappedContext* context, DWORD err) {
    if (context != nullptr) {
      channel_->decr_io_count();

      if (context->op == OperationType::kConnect) {
        delete static_cast<ConnectContext*>(context);
      }
      else if (context->op == OperationType::kRead) {
        read_pending_.store(false, std::memory_order_release);
        ReadContext* read_context = static_cast<ReadContext*>(context);
        if (read_ol_context_ == read_context) { read_ol_context_ = nullptr; }
        poll_->read_pool().release(read_context);
      }
      else if (context->op == OperationType::kWrite) {
        WriteContext* write_context = static_cast<WriteContext*>(context);
        delete write_context->associated_buffer;
        write_context->associated_buffer = nullptr;
        if (write_ol_context_ == write_context) { write_ol_context_ = nullptr; }
        write_in_progress_.store(false, std::memory_order_release);
        poll_->write_pool().release(write_context);
      }
    }

    if (cleanup_done_.load(std::memory_order_acquire)) {
      if (channel_ && channel_->pending_io_count() <= 0) {
        do_cleanup_(cleanup_err_);
      }
      return;
    }

    cleanup_(err);
  }

  void IOCPConn::cleanup_(DWORD err) {
    bool expected = false;
    if (!cleanup_done_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return;
    }

    cancel_close_timeout_();

    if (get_state_() == State::kConnected && handle_ != invalid_socket) {
      ::shutdown(handle_, SD_SEND);
    }

    set_state_(State::kDisconnecting);

    // 如果有 pending IO，取消并等待
    if (channel_ && channel_->pending_io_count() > 0) {
      CancelIoEx(reinterpret_cast<HANDLE>(handle_), nullptr);
      cleanup_err_ = err;
      return;
    }

    do_cleanup_(err);
  }

  void IOCPConn::do_cleanup_(DWORD err) {
    if (handle_ != invalid_socket) {
      closesocket(handle_);
      handle_ = invalid_socket;
    }

    set_state_(State::kDisconnected);
    channel_.reset();

    if (internal_close_func_ != nullptr) { internal_close_func_(err); }
    if (on_close_func_ != nullptr) { on_close_func_(err); }
  }

  void IOCPConn::start_close_timeout_() {
    if (close_timer_id_ != 0) { return; }

    // 使用 weak_ptr 避免循环引用和野指针
    std::weak_ptr<IOCPConn> weak_self = shared_from_this();
    close_timer_id_                   = poll_->timer_manager()->add_timer(
      close_timeout_ms_,
      [weak_self]() {
        // 检查对象是否仍然存活
        if (auto self = weak_self.lock()) {
          self->cleanup_(WAIT_TIMEOUT); // 使用 WAIT_TIMEOUT 作为超时错误码
        }
      });
  }

  void IOCPConn::cancel_close_timeout_() {
    if (close_timer_id_ != 0) {
      poll_->timer_manager()->cancel_timer(close_timer_id_);
      close_timer_id_ = 0;
    }
  }
} // namespace iocpnet
