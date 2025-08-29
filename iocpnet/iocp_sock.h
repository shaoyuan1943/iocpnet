#ifndef IOCP_WIN_SOCK_H
#define IOCP_WIN_SOCK_H

#include <WinSock2.h>

#include <MSWSock.h>
#include <WS2tcpip.h>
#include <Windows.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

#include <functional>
#include <memory>
#include <stdint.h>
#include <string>

namespace iocpnet {
  class IOCPConn;
  class IOCPEventPoll;
  class Buffer;
  // type define
  using socket_t = SOCKET;
  // const values
  static constexpr socket_t invalid_socket = INVALID_SOCKET;
  static constexpr uint32_t kPollTimeoutMS = 10;

  // clang-format off
  enum class ProtocolStack { kIPv4Only, kIPv6Only, kDualStack };
  enum class IPType { kInvalid, kIPv4, kIPv6 };
  enum class RunningMode { kOnePollPerThread, kAllOneThread };
  enum class State { kDisconnected, kConnecting, kConnected, kDisconnecting };
  // clang-format on

  namespace SocketOption {
    static const int kNone      = 0;
    static const int kReusePort = 1 << 0;
    static const int kReuseAddr = 1 << 1;
  } // namespace SocketOption

  using ConnPtr = std::shared_ptr<IOCPConn>;
  using Closure = std::function<void()>;

  class NonCopyable {
  public:
    NonCopyable(const NonCopyable&)            = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&)                 = delete;
    NonCopyable& operator=(NonCopyable&&)      = delete;
  protected:
    NonCopyable()  = default;
    ~NonCopyable() = default;
  };

  bool             set_non_blocking(socket_t handle);
  IPType           ip_address_type(const char* address);
  sockaddr_storage get_sockaddr(const char* address, uint16_t port, ProtocolStack stack);
} // namespace iocpnet

#endif // IOCP_WIN_SOCK_H
