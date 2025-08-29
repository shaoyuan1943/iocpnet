#ifndef IOCP_EVENT_LOOP_H
#define IOCP_EVENT_LOOP_H

#include "iocp_sock.h"

#include <mutex>
#include <thread>

namespace iocpnet {
  class OverlappedContext;
  class IOCPEventPoll : public NonCopyable {
  public:
    IOCPEventPoll(HANDLE handle);
    ~IOCPEventPoll();

    void shutdown();
    bool register_in(socket_t handle, OverlappedContext* context);
    void run();
    void poll();

    void   set_name(std::string_view name) { name_ = name; }
    bool   is_in_poll_thread() const { return thread_id_ == std::this_thread::get_id(); }
    void   set_poll_error_callback(std::function<void(IOCPEventPoll*, DWORD)> func) { on_err_func_ = std::move(func); }
    HANDLE iocp_handle() const { return iocp_handle_; }
  private:
    void _poll(uint32_t poll_timeout = 0);
  private:
    HANDLE                                     iocp_handle_;
    std::atomic<bool>                          shut_;
    std::thread::id                            thread_id_;
    std::string                                name_;
    std::mutex                                 mutex_;
    std::function<void(IOCPEventPoll*, DWORD)> on_err_func_;
  };
} // namespace iocpnet

#endif