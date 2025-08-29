#ifndef IOCP_CONN_H
#define IOCP_CONN_H

#include "iocp_channel.h"
#include "iocp_sock.h"

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
    IOCPConn(socket_t handle);
    ~IOCPConn();

    void        shutdown();
    void        send(const char* msg, size_t size);
    std::string state_string() const;
    socket_t    native_handle() const { return handle_; }
    void        set_remote_addr(const char* addr, uint16_t port) {
      memcpy(addr_, addr, INET6_ADDRSTRLEN);
      port_ = port;
    }
    std::pair<const char*, uint16_t> remote_addr_and_port() { return std::make_pair(addr_, port_); }
    bool                             connected() const { return _state() == State::kConnected; }
    void                             set_conn_user_callbacks(std::function<void(Buffer*)> msg_func, std::function<void(DWORD)> close_func) {
      on_message_func_ = std::move(msg_func);
      on_close_func_   = std::move(close_func);
    }
  private:
    void  _start();
    void  _set_state(State e) { state_.store(static_cast<int>(e), std::memory_order_release); }
    State _state() const { return static_cast<State>(state_.load(std::memory_order_acquire)); }
    void  _on_completion_error(OverlappedContext* context, DWORD err);
    void  _on_completion_read(ReadContext* context, DWORD bytes_transferred);
    void  _on_completion_write(WriteContext* context, DWORD bytes_transferred);
    void  _go_reading();
    void  _go_writing();
  private:
    socket_t                     handle_;
    char                         addr_[INET6_ADDRSTRLEN];
    uint16_t                     port_;
    std::atomic<int>             state_;
    std::unique_ptr<Buffer>      read_buffer_;
    bool                         read_pending_;
    ReadContext*                 read_ol_context_;
    WriteContext*                write_ol_context_;
    std::unique_ptr<IOCPChannel> channel_;
    std::function<void(DWORD)>   on_close_func_;
    std::function<void(Buffer*)> on_message_func_;
    std::deque<Buffer*>          send_queue_;
    std::mutex                   mutex_;
    std::atomic_int              standing_sends_;
  };
} // namespace iocpnet
#endif // IOCP_CONN_H
