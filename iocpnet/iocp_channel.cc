#include "iocp_channel.h"
#include "iocp_context.h"

namespace iocpnet {
  IOCPChannel::IOCPChannel(socket_t handle)
      : handle_ {handle}
      , tied_ {false} {
  }

  IOCPChannel::~IOCPChannel() {}

  void IOCPChannel::tie(const std::shared_ptr<void>& ptr) {
    tie_  = ptr;
    tied_ = true;
  }

  void IOCPChannel::handle_completion_error(OVERLAPPED* ol, DWORD err) {
    OverlappedContext* context = CONTAINING_RECORD(ol, OverlappedContext, ol);
    if (on_err_func_ != nullptr) { on_err_func_(context, err); }
  }

  void IOCPChannel::handle_completion(OVERLAPPED* ol, DWORD bytes_transferred) {
    OverlappedContext* context = CONTAINING_RECORD(ol, OverlappedContext, ol);
    if (context->op == OperationType::kAccept) {
      if (on_accept_func_ != nullptr) { on_accept_func_(static_cast<AcceptContext*>(context), bytes_transferred); }
    }

    if (context->op == OperationType::kRead) {
      if (on_read_func_ != nullptr) { on_read_func_(static_cast<ReadContext*>(context), bytes_transferred); }
    }

    if (context->op == OperationType::kWrite) {
      if (on_write_func_ != nullptr) { on_write_func_(static_cast<WriteContext*>(context), bytes_transferred); }
    }

    if (context->op == OperationType::kConnect) {
      if (on_connect_func_ != nullptr) { on_connect_func_(static_cast<ConnectContext*>(context)); }
    }
  }
} // namespace iocpnet