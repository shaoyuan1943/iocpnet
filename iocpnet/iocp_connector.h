#ifndef IOCP_CONNECTOR_H
#define IOCP_CONNECTOR_H

#include "iocp_sock.h"

namespace iocpnet {
  struct OverlappedContext;
  struct ConnectContext;
  class IOCPEventPoll;
  class IOCPChannel;
  class IOCPConnector : public NonCopyable
      , public std::enable_shared_from_this<IOCPConnector> {
  public:
    IOCPConnector(IOCPEventPoll* poll, std::string_view addr, uint16_t port);
    ~IOCPConnector();

    void shutdown();
    bool start();
    void set_conn_callback(std::function<void(socket_t, sockaddr_storage)> func) { on_conn_func_ = std::move(func); };
    void set_error_callback(std::function<void(DWORD)> func) { on_err_func_ = std::move(func); }
  private:
    void _on_completion_connect(ConnectContext* context);
    void _on_completion_error(OverlappedContext* context, DWORD err);
  private:
    IOCPEventPoll*                                  event_poll_;
    HANDLE                                          iocp_handle_;
    std::string                                     addr_;
    uint16_t                                        port_;
    sockaddr_storage                                addr_storage_;
    socket_t                                        handle_;
    ConnectContext*                                 connect_context_;
    std::unique_ptr<IOCPChannel>                    channel_;
    std::function<void(socket_t, sockaddr_storage)> on_conn_func_;
    std::function<void(DWORD)>                      on_err_func_;
  };
} // namespace iocpnet

#endif // IOCP_CONNECTOR_H
