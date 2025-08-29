#include "iocp_sock.h"

namespace iocpnet {
  bool set_non_blocking(socket_t handle) {
    u_long option = 1;
    return ioctlsocket(handle, FIONBIO, &option) == 0;
  }

  IPType ip_address_type(const char* address) {
    if (address == nullptr) { return IPType::kInvalid; }

    sockaddr_in sa {};
    if (inet_pton(AF_INET, address, &(sa.sin_addr)) == 1) {
      return IPType::kIPv4;
    }

    sockaddr_in6 sa6 {};
    if (inet_pton(AF_INET6, address, &(sa6.sin6_addr)) == 1) {
      return IPType::kIPv6;
    }

    return IPType::kInvalid;
  }

  sockaddr_storage get_sockaddr(const char* address, uint16_t port, ProtocolStack stack) {
    sockaddr_storage addr_storage = {};

    IPType ip_type = ip_address_type(address);
    if (ip_type == IPType::kInvalid) { return addr_storage; }

    int af_family = 0;
    if (ip_type == IPType::kIPv4 && stack == ProtocolStack::kIPv4Only) {
      af_family = AF_INET;
    }

    if (ip_type == IPType::kIPv6 && stack == ProtocolStack::kIPv6Only) {
      af_family = AF_INET6;
    }

    if (ip_type == IPType::kIPv6 && stack == ProtocolStack::kDualStack) {
      af_family = AF_INET6; // supports both IPv4 and IPv6 simultaneously
    }

    if (af_family == 0) { return addr_storage; }

    if (af_family == AF_INET) {
      sockaddr_in* sockaddr_info = reinterpret_cast<sockaddr_in*>(&addr_storage);
      sockaddr_info->sin_family  = AF_INET;
      sockaddr_info->sin_port    = htons(port);
      if (inet_pton(AF_INET, address, &sockaddr_info->sin_addr) <= 0) {
        addr_storage.ss_family = 0;
        return addr_storage;
      }
    }

    if (af_family == AF_INET6) {
      sockaddr_in6* sockaddr_info6 = reinterpret_cast<sockaddr_in6*>(&addr_storage);
      sockaddr_info6->sin6_family  = AF_INET6;
      sockaddr_info6->sin6_port    = htons(port);
      if (inet_pton(AF_INET6, address, &sockaddr_info6->sin6_addr) <= 0) {
        addr_storage.ss_family = 0;
        return addr_storage;
      }
    }

    return addr_storage;
  }
} // namespace iocpnet