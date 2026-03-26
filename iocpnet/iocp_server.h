#ifndef IOCP_SERVER_H
#define IOCP_SERVER_H

#include "iocp_context.h"
#include "iocp_sock.h"

#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace iocpnet {
  class IOCPEventPoll;
  class IOCPAcceptor;
  class IOCPConn;
  class IOCPServer : public NonCopyable {
  public:
    IOCPServer(std::string_view addr, uint16_t port,
               ProtocolStack proto_stack = ProtocolStack::kIPv4Only,
               int           option      = SocketOption::kNone);
    ~IOCPServer();

    // 优雅关闭：停止accept → 优雅关闭连接 → 等待完成或超时 → 释放资源
    void shutdown();
    // 立即关闭：立即释放所有资源
    void close();

    bool start(RunningMode mode = RunningMode::kAllOneThread, int n = 0);
    void run();
    void poll();

    void set_conn_user_callback(std::function<void(ConnPtr)>&& func) { on_conn_func_ = std::move(func); }
    void set_error_user_callback(std::function<void(DWORD)>&& func) { on_err_func_ = std::move(func); }
    void set_max_connections(size_t max_connections) { max_connections_ = max_connections; }
    void set_shutdown_timeout(uint32_t ms) { shutdown_timeout_ms_ = ms; }

    size_t connection_count() const {
      std::lock_guard locker(mutex_);
      return conns_.size();
    }
  private:
    void on_new_connection_(socket_t handle, sockaddr_storage remote_addr);
    void on_accept_error_(DWORD err);
    void shutdown_polls_and_handle_();
  private:
    sockaddr_storage                                        listen_addr_;
    ProtocolStack                                           proto_stack_;
    int                                                     sock_option_;
    HANDLE                                                  handle_;
    std::shared_ptr<IOCPEventPoll>                          main_poll_;
    int                                                     thread_num_;
    bool                                                    started_;
    RunningMode                                             running_mode_;
    std::unordered_map<socket_t, std::shared_ptr<IOCPConn>> conns_;
    mutable std::mutex                                      mutex_;
    std::vector<std::shared_ptr<IOCPEventPoll>>             sub_poll_objs_;
    std::vector<std::unique_ptr<std::thread>>               sub_polls_;
    std::shared_ptr<IOCPAcceptor>                           acceptor_;
    std::function<void(ConnPtr)>                            on_conn_func_;
    std::function<void(DWORD)>                              on_err_func_;
    size_t                                                  max_connections_    = 0;  // 0 表示无限制
    uint32_t                                                shutdown_timeout_ms_ = 30000; // 默认 30 秒
  };
} // namespace iocpnet

#endif
