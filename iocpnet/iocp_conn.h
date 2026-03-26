#ifndef IOCP_CONN_H
#define IOCP_CONN_H

#include "iocp_channel.h"
#include "iocp_sock.h"
#include "iocp_timer.h"

#include <deque>
#include <memory>
#include <mutex>

namespace iocpnet {
  class Buffer;
  struct ReadContext;
  struct WriteContext;
  struct OverlappedContext;
  class IOCPChannel;
  class IOCPConn : public NonCopyable
      , public std::enable_shared_from_this<IOCPConn> {
  public:
    friend class IOCPServer;
    IOCPConn(IOCPEventPoll* poll, socket_t handle = invalid_socket);
    ~IOCPConn();

    bool        connect(std::string_view addr, uint16_t port);
    bool        start();
    void        shutdown(); // 优雅关闭：半关闭，等待对端关闭或超时
    void        close();    // 强制关闭：立即清理资源
    void        send(const char* msg, size_t size);
    std::string state_string() const;
    socket_t    native_handle() const { return handle_; }
    std::pair<const char*, uint16_t> remote_addr_and_port() { return std::make_pair(addr_, port_); }
    bool                             connected() const { return get_state_() == State::kConnected; }
    void                             set_conn_user_callbacks(std::function<void(Buffer*)> msg_func, std::function<void(DWORD)> close_func) {
      on_message_func_ = std::move(msg_func);
      on_close_func_   = std::move(close_func);
    }
    void set_close_timeout(uint32_t ms) { close_timeout_ms_ = ms; }
  private:
    void set_internal_close_callback_(std::function<void(DWORD)> close_func) {
      internal_close_func_ = std::move(close_func);
    }
    void set_remote_addr(const char* addr, uint16_t port) {
      memcpy(addr_, addr, INET6_ADDRSTRLEN);
      port_ = port;
    }
    void  set_state_(State e) { state_value_.store(static_cast<int>(e), std::memory_order_release); }
    State get_state_() const { return static_cast<State>(state_value_.load(std::memory_order_acquire)); }
    void  on_completion_error_(OverlappedContext* context, DWORD err);
    void  on_completion_read_(ReadContext* context, DWORD bytes_transferred);
    void  on_completion_write_(WriteContext* context, DWORD bytes_transferred);
    void  on_completion_connect_(ConnectContext* context);
    void  go_reading_();
    void  go_writing_();

    // close
    void cleanup_(DWORD err);
    void do_cleanup_(DWORD err);
    void start_close_timeout_();
    void cancel_close_timeout_();
  private:
    IOCPEventPoll*               poll_;
    socket_t                     handle_;
    sockaddr_storage             addr_storage_;
    char                         addr_[INET6_ADDRSTRLEN];
    uint16_t                     port_;
    std::atomic<int>             state_value_;
    std::unique_ptr<Buffer>      read_buffer_;
    std::atomic_bool             read_pending_;
    ReadContext*                 read_ol_context_;
    WriteContext*                write_ol_context_;
    std::unique_ptr<IOCPChannel> channel_;
    std::function<void(DWORD)>   on_close_func_;
    std::function<void(DWORD)>   internal_close_func_;
    std::function<void(Buffer*)> on_message_func_;
    std::deque<Buffer*>          send_queue_;
    std::mutex                   mutex_;
    std::atomic_bool             write_in_progress_;

    // close
    uint32_t         close_timeout_ms_ {30000};
    DWORD            cleanup_err_ {0};
    Timer::TimerID   close_timer_id_ {0};
    std::atomic_bool cleanup_done_ {false};
  };
} // namespace iocpnet
#endif // IOCP_CONN_H
