#ifndef IOCP_SERVER_H
#define IOCP_SERVER_H

#include "iocp_context.h"
#include "iocp_sock.h"

#include <mutex>
#include <thread>

namespace iocpnet {
  class IOCPEventPoll;
  class IOCPAcceptor;
  class IOCPConn;
  class IOCPServer : public NonCopyable {
  public:
    IOCPServer(std::string_view addr, uint16_t port, ProtocolStack proto_stack = ProtocolStack::kIPv4Only, int option = SocketOption::kNone);
    ~IOCPServer();

    void shutdown();
    bool start(RunningMode mode = RunningMode::kAllOneThread, int n = 0);
    void run();
    void poll();

    void set_conn_user_callback(std::function<void(ConnPtr)> func) { on_conn_func_ = std::move(func); }
    void set_error_user_callback(std::function<void(DWORD)> func) { on_err_func_ = std::move(func); }
  private:
    void _on_new_connection(socket_t handle, sockaddr_storage remote_addr);
    void _on_accept_error(DWORD err);
  private:
    sockaddr_storage                                   listen_addr_;
    ProtocolStack                                      proto_stack_;
    int                                                sock_option_;
    HANDLE                                             iocp_handle_;
    std::unique_ptr<IOCPEventPoll>                     main_poll_;
    int                                                thread_num_;
    bool                                               started_;
    RunningMode                                        running_mode_;
    std::unordered_map<int, std::shared_ptr<IOCPConn>> conns_;
    std::mutex                                         mutex_;
    std::vector<std::unique_ptr<std::thread>>          sub_polls_;
    std::shared_ptr<IOCPAcceptor>                      acceptor_;
    std::function<void(ConnPtr)>                       on_conn_func_;
    std::function<void(DWORD)>                         on_err_func_;
  };
} // namespace iocpnet

#endif
