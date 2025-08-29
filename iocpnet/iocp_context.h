#ifndef IOCP_CONTEXT_H
#define IOCP_CONTEXT_H

#include "buffer.h"
#include "iocp_sock.h"

namespace iocpnet {
  class IOCPChannel;
  class Buffer;

  // clang-format off
  enum class OperationType { kAccept, kConnect, kRead, kWrite };
  // clang-format on
  struct OverlappedContext {
    OVERLAPPED    ol;
    OperationType op;
    IOCPChannel*  channel;
    DWORD         flags;

    OverlappedContext(OperationType opt)
        : op {opt}
        , channel {nullptr}
        , flags {0} {
      ZeroMemory(&ol, sizeof(ol));
    }

    virtual ~OverlappedContext() = default;
  };

  struct AcceptContext : public OverlappedContext {
    socket_t listen_handle;
    socket_t accept_handle;
    char     sockaddr_buffer[sizeof(sockaddr_storage) + 16 + sizeof(sockaddr_storage) + 16];

    AcceptContext(socket_t handle)
        : OverlappedContext(OperationType::kAccept)
        , listen_handle {handle}
        , accept_handle {invalid_socket}
        , sockaddr_buffer {0} {}
  };

  struct ConnectContext : public OverlappedContext {
    ConnectContext()
        : OverlappedContext(OperationType::kConnect) {}
  };

  struct ReadContext : public OverlappedContext {
    WSABUF wsa;
    ReadContext()
        : OverlappedContext(OperationType::kRead) {}
  };

  struct WriteContext : public OverlappedContext {
    WSABUF  wsa;
    Buffer* associated_buffer;
    WriteContext()
        : OverlappedContext(OperationType::kWrite)
        , associated_buffer {nullptr} {}
  };
} // namespace iocpnet

#endif