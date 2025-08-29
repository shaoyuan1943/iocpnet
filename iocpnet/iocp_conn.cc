#include "iocp_conn.h"
#include "buffer.h"
#include "iocp_channel.h"
#include "iocp_context.h"

#include <mutex>

namespace iocpnet {
  IOCPConn::IOCPConn(socket_t handle)
      : handle_ {handle}
      , state_ {static_cast<int>(State::kDisconnecting)}
      , addr_ {0}
      , port_ {0}
      , read_pending_ {false}
      , read_ol_context_ {nullptr}
      , write_ol_context_ {nullptr}
      , standing_sends_ {0} {
  }

  IOCPConn::~IOCPConn() {
  }

  void IOCPConn::shutdown() {
    if (_state() == State::kDisconnected || _state() == State::kDisconnecting) { return; }
    if (handle_ == invalid_socket) { return; }

    ::shutdown(handle_, SD_SEND);
    _set_state(State::kDisconnecting);
  }

  void IOCPConn::send(const char* msg, size_t size) {
    if (!connected() || msg == nullptr || size == 0) { return; }

    Buffer* buf = new Buffer(msg, size);
    {
      std::lock_guard<std::mutex> locker(mutex_);
      send_queue_.push_back(buf);
    }

    if (standing_sends_.fetch_add(1, std::memory_order_release) == 0) {
      _go_writing();
    }
  }

  void IOCPConn::_start() {
    if (connected()) { return; }

    read_buffer_ = std::make_unique<Buffer>();
    channel_     = std::make_unique<IOCPChannel>(handle_);
    channel_->set_error_callback(std::bind(&IOCPConn::_on_completion_error, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
    channel_->set_read_callback(std::bind(&IOCPConn::_on_completion_read, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
    channel_->set_write_callback(std::bind(&IOCPConn::_on_completion_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
    channel_->tie(shared_from_this());

    _set_state(State::kConnected);
    _go_reading();
  }

  void IOCPConn::_go_reading() {
    if (!connected() || read_pending_) { return; }

    read_ol_context_          = new ReadContext();
    read_ol_context_->wsa.buf = read_buffer_->to_write();
    read_ol_context_->wsa.len = read_buffer_->writable_size();
    read_ol_context_->channel = channel_.get();

    if (WSARecv(handle_, &read_ol_context_->wsa, 1, NULL,
                &read_ol_context_->flags, &read_ol_context_->ol, NULL) == SOCKET_ERROR) {
      if (DWORD err = WSAGetLastError(); err != WSA_IO_PENDING) {
        _on_completion_error(read_ol_context_, err);
        read_ol_context_ = nullptr;
        return;
      }
    }

    channel_->incr_io_count();
    read_pending_ = true;
  }

  void IOCPConn::_go_writing() {
    if (standing_sends_.load(std::memory_order_acquire) <= 0) { return; }

    Buffer* send_buf = nullptr;
    {
      std::lock_guard<std::mutex> locker(mutex_);
      send_buf = send_queue_.front();
      send_queue_.pop_front();
    }

    write_ol_context_                    = new WriteContext;
    write_ol_context_->wsa.buf           = send_buf->to_read();
    write_ol_context_->wsa.len           = send_buf->readable_size();
    write_ol_context_->associated_buffer = send_buf;
    write_ol_context_->channel           = channel_.get();

    DWORD sent = 0;
    if (WSASend(handle_, &write_ol_context_->wsa, 1, &sent, 0,
                &write_ol_context_->ol, nullptr) == SOCKET_ERROR) {
      if (DWORD err = WSAGetLastError(); err != WSA_IO_PENDING) {
        _on_completion_error(write_ol_context_, err);
        write_ol_context_ = nullptr;
      }
    }

    channel_->incr_io_count();
  }

  std::string IOCPConn::state_string() const {
    switch (State e = _state()) {
    case State::kDisconnected:
      return "Disconnected";
    case State::kConnecting:
      return "Connecting";
    case State::kConnected:
      return "Connected";
    case State::kDisconnecting:
      return "Disconnecting";
    default:
      return "Unknow";
    }
  }

  void IOCPConn::_on_completion_read(ReadContext* context, DWORD bytes_transferred) {
    std::unique_ptr<ReadContext> ptr(context);

    if (bytes_transferred == 0) {
      shared_from_this()->shutdown();
      return;
    }

    channel_->decr_io_count();
    read_pending_ = false;
    read_buffer_->been_written(bytes_transferred);
    _go_reading();
    if (on_message_func_ != nullptr) { on_message_func_(read_buffer_.get()); }
  }

  void IOCPConn::_on_completion_write(WriteContext* context, DWORD bytes_transferred) {
    std::unique_ptr<WriteContext> ptr(context);

    channel_->decr_io_count();
    delete ptr->associated_buffer;
    standing_sends_.fetch_add(-1, std::memory_order_release);
    if (standing_sends_.load(std::memory_order_acquire) > 0) {
      _go_writing();
    }
  }

  void IOCPConn::_on_completion_error(OverlappedContext* context, DWORD err) {
    std::unique_ptr<OverlappedContext> ptr(context);
    if (_state() == State::kDisconnected) { return; }

    channel_->decr_io_count();

    auto close_func = [self = shared_from_this(), context, err]() {
      closesocket(self->handle_);
      self->handle_ = invalid_socket;
      self->_set_state(State::kDisconnected);
      self->channel_.reset();

      if (context->op == OperationType::kWrite) {
        self->write_ol_context_ = nullptr;
      }

      if (context->op == OperationType::kRead) {
        self->read_ol_context_ = nullptr;
      }

      if (self->on_close_func_ != nullptr) { self->on_close_func_(err); }
    };

    if (err == ERROR_OPERATION_ABORTED) {
      if (channel_->pending_io_count() <= 0) {
        close_func();
      }
    } else {
      close_func();
    }
  }
} // namespace iocpnet