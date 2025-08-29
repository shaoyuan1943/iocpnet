#ifndef IOCP_ACCEPTOR_H
#define IOCP_ACCEPTOR_H

#include "iocp_sock.h"

namespace iocpnet {
  struct OverlappedContext;
  class IOCPEventPoll;
  class AcceptContext;
  class IOCPChannel;
  class IOCPAcceptor : public NonCopyable
      , public std::enable_shared_from_this<IOCPAcceptor> {
  public:
    explicit IOCPAcceptor(IOCPEventPoll* poll);
    ~IOCPAcceptor();

    void shutdown();
    bool start(int accept_n, sockaddr_storage addr, ProtocolStack protocol_stack = ProtocolStack::kIPv4Only,
               int option = SocketOption::kNone);
    bool listening() const { return listen_handle_ != invalid_socket; }
    void set_conn_callback(std::function<void(socket_t, sockaddr_storage)> func) { on_conn_func_ = std::move(func); };
    void set_error_callback(std::function<void(DWORD)> func) { on_err_func_ = std::move(func); }
  private:
    void _on_completion_accept(AcceptContext* context, DWORD bytes_transferred);
    void _on_completion_error(OverlappedContext* context, DWORD err);
    void _go_accepting();
  private:
    IOCPEventPoll*                                  event_poll_;
    HANDLE                                          iocp_handle_;
    socket_t                                        listen_handle_;
    sockaddr_storage                                listen_sockaddr_;
    std::unique_ptr<IOCPChannel>                    channel_;
    std::function<void(socket_t, sockaddr_storage)> on_conn_func_;
    std::function<void(DWORD)>                      on_err_func_;
  };
} // namespace iocpnet

#endif // IOCP_ACCEPTOR_H
