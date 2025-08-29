#include "iocp_sockex_func.h"

namespace iocpnet {
  LPFN_ACCEPTEX SockExFunc::load_acceptex_func(socket_t handle) {
    LPFN_ACCEPTEX acceptex_func  = NULL;
    GUID          guid_acceptex  = WSAID_ACCEPTEX;
    DWORD         bytes_returned = 0;
    int           result         = WSAIoctl(handle, SIO_GET_EXTENSION_FUNCTION_POINTER,
                                            &guid_acceptex, sizeof(guid_acceptex),
                                            &acceptex_func, sizeof(acceptex_func),
                                            &bytes_returned, NULL, NULL);
    if (result == SOCKET_ERROR) { return NULL; }

    return acceptex_func;
  }

  LPFN_CONNECTEX SockExFunc::load_connectex_func(socket_t handle) {
    LPFN_CONNECTEX connectex_func = NULL;
    GUID           guid_connectex = WSAID_CONNECTEX;
    DWORD          bytes_returned = 0;
    int            result         = WSAIoctl(handle, SIO_GET_EXTENSION_FUNCTION_POINTER,
                                             &guid_connectex, sizeof(guid_connectex),
                                             &connectex_func, sizeof(connectex_func),
                                             &bytes_returned, NULL, NULL);
    if (result == SOCKET_ERROR) { return NULL; }

    return connectex_func;
  }
} // namespace iocpnet