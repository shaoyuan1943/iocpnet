#ifndef IOCP_CHANNEL_H
#define IOCP_CHANNEL_H

#include "iocp_context.h"
#include "iocp_sock.h"

#include <memory>

namespace iocpnet {
  struct AcceptContext;
  struct ReadContext;
  class OverlappedContext;
  class IOCPChannel {
  public:
    IOCPChannel(socket_t handle);
    ~IOCPChannel();

    void handle_completion(OVERLAPPED* ol, DWORD bytes_transferred);
    void handle_completion_error(OVERLAPPED* ol, DWORD err);

    void incr_io_count() { io_pending_count_.fetch_add(1); }
    void decr_io_count() {
      if (io_pending_count_.load(std::memory_order_acquire) > 0) {
        io_pending_count_.fetch_add(-1, std::memory_order_release);
      }
    }
    int  pending_io_count() const { return io_pending_count_.load(std::memory_order_acquire); }
    void tie(const std::shared_ptr<void>& ptr);
    bool is_tied() const { return tied_; }

    void set_accept_callback(std::function<void(AcceptContext*, DWORD)> func) { on_accept_func_ = std::move(func); }
    void set_read_callback(std::function<void(ReadContext*, DWORD)> func) { on_read_func_ = std::move(func); }
    void set_write_callback(std::function<void(WriteContext*, DWORD)> func) { on_write_func_ = std::move(func); }
    void set_connect_callback(std::function<void(ConnectContext*)> func) { on_connect_func_ = std::move(func); }
    void set_error_callback(std::function<void(OverlappedContext*, DWORD)> func) { on_err_func_ = std::move(func); }
  private:
    socket_t            handle_;
    bool                tied_;
    std::weak_ptr<void> tie_;

    std::function<void(AcceptContext*, DWORD)>     on_accept_func_;
    std::function<void(OverlappedContext*, DWORD)> on_err_func_;
    std::function<void(ReadContext*, DWORD)>       on_read_func_;
    std::function<void(WriteContext*, DWORD)>      on_write_func_;
    std::function<void(ConnectContext*)>           on_connect_func_;
    std::atomic_int                                io_pending_count_;
  };
} // namespace iocpnet

#endif