#ifndef IOCP_SOCKEX_FUNC_H
#define IOCP_SOCKEX_FUNC_H

#include "iocp_sock.h"

namespace iocpnet {
  class SockExFunc {
  public:
    static LPFN_ACCEPTEX load_acceptex_func(socket_t handle);
    static LPFN_CONNECTEX load_connectex_func(socket_t handle);
  };
}

#endif