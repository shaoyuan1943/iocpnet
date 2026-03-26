#include "iocp_sockex_func.h"

#include <mutex>

namespace iocpnet {
  namespace {
    std::mutex                g_mutex;
    uint32_t                  g_init_count   = 0;
    LPFN_ACCEPTEX             g_acceptex     = nullptr;
    LPFN_CONNECTEX            g_connectex    = nullptr;
    LPFN_GETACCEPTEXSOCKADDRS g_getsockaddrs = nullptr;

    socket_t create_probe_socket_() {
      socket_t handle = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
      if (handle != invalid_socket) { return handle; }

      return WSASocketW(AF_INET6, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    }
  } // namespace

  bool SockExFunc::init() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_init_count++ > 0) { return true; }

    WSADATA wsa_data {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
      g_init_count = 0;
      return false;
    }

    socket_t probe = create_probe_socket_();
    if (probe == invalid_socket) {
      WSACleanup();
      g_init_count = 0;
      return false;
    }

    g_acceptex     = load_acceptex_func(probe);
    g_connectex    = load_connectex_func(probe);
    g_getsockaddrs = load_getacceptexsockaddrs_func(probe);
    closesocket(probe);

    if (g_acceptex == nullptr || g_connectex == nullptr || g_getsockaddrs == nullptr) {
      g_acceptex     = nullptr;
      g_connectex    = nullptr;
      g_getsockaddrs = nullptr;
      WSACleanup();
      g_init_count = 0;
      return false;
    }

    return true;
  }

  void SockExFunc::cleanup() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_init_count == 0) { return; }

    --g_init_count;
    if (g_init_count > 0) { return; }

    g_acceptex     = nullptr;
    g_connectex    = nullptr;
    g_getsockaddrs = nullptr;
    WSACleanup();
  }

  LPFN_ACCEPTEX SockExFunc::load_acceptex_func(socket_t handle) {
    LPFN_ACCEPTEX acceptex_func  = nullptr;
    GUID          guid_acceptex  = WSAID_ACCEPTEX;
    DWORD         bytes_returned = 0;
    int           result         = WSAIoctl(handle, SIO_GET_EXTENSION_FUNCTION_POINTER,
                                            &guid_acceptex, sizeof(guid_acceptex),
                                            &acceptex_func, sizeof(acceptex_func),
                                            &bytes_returned, nullptr, nullptr);
    if (result == SOCKET_ERROR) { return nullptr; }

    return acceptex_func;
  }

  LPFN_CONNECTEX SockExFunc::load_connectex_func(socket_t handle) {
    LPFN_CONNECTEX connectex_func = nullptr;
    GUID           guid_connectex = WSAID_CONNECTEX;
    DWORD          bytes_returned = 0;
    int            result         = WSAIoctl(handle, SIO_GET_EXTENSION_FUNCTION_POINTER,
                                             &guid_connectex, sizeof(guid_connectex),
                                             &connectex_func, sizeof(connectex_func),
                                             &bytes_returned, nullptr, nullptr);
    if (result == SOCKET_ERROR) { return nullptr; }

    return connectex_func;
  }

  LPFN_GETACCEPTEXSOCKADDRS SockExFunc::load_getacceptexsockaddrs_func(socket_t handle) {
    LPFN_GETACCEPTEXSOCKADDRS getsockaddrs_func = nullptr;
    GUID                      guid_getsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    DWORD                     bytes_returned    = 0;
    int                       result            = WSAIoctl(handle, SIO_GET_EXTENSION_FUNCTION_POINTER,
                                                           &guid_getsockaddrs, sizeof(guid_getsockaddrs),
                                                           &getsockaddrs_func, sizeof(getsockaddrs_func),
                                                           &bytes_returned, nullptr, nullptr);
    if (result == SOCKET_ERROR) { return nullptr; }

    return getsockaddrs_func;
  }

  LPFN_ACCEPTEX SockExFunc::acceptex() { return g_acceptex; }

  LPFN_CONNECTEX SockExFunc::connectex() { return g_connectex; }

  LPFN_GETACCEPTEXSOCKADDRS SockExFunc::getsockaddrs() { return g_getsockaddrs; }
} // namespace iocpnet
