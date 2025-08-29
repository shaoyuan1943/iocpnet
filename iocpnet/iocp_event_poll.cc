#include "iocp_event_poll.h"
#include "iocp_channel.h"
#include "iocp_context.h"

namespace iocpnet {
  IOCPEventPoll::IOCPEventPoll(HANDLE handle)
      : iocp_handle_ {handle}
      , shut_ {false}
      , on_err_func_ {nullptr} {
    thread_id_ = std::this_thread::get_id();
  }

  IOCPEventPoll::~IOCPEventPoll() {
    shutdown();
  }

  void IOCPEventPoll::shutdown() {
    if (shut_.load(std::memory_order_acquire)) {
      return;
    }

    shut_.store(true, std::memory_order_release);
    PostQueuedCompletionStatus(iocp_handle_, 0, 0, nullptr);
  }

  bool IOCPEventPoll::register_in(socket_t handle, OverlappedContext* context) {
    HANDLE result = INVALID_HANDLE_VALUE;
    if (context == nullptr) {
      result = CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket),
                                      iocp_handle_,
                                      reinterpret_cast<ULONG_PTR>(socket),
                                      0);
    } else {
      result = CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket),
                                      iocp_handle_,
                                      reinterpret_cast<ULONG_PTR>(context),
                                      0);
    }

    return result != NULL && result == iocp_handle_;
  }

  void IOCPEventPoll::poll() {
    if (shut_.load(std::memory_order_acquire)) { return; }
    _poll(kPollTimeoutMS);
  }

  void IOCPEventPoll::run() {
    thread_id_ = std::this_thread::get_id();
    while (!shut_.load(std::memory_order_acquire)) {
      _poll(0); // always blocking
    }
  }

  void IOCPEventPoll::_poll(uint32_t poll_timeout) {
    DWORD        bytes_transferred = 0;
    ULONG_PTR    completion_key    = 0;
    LPOVERLAPPED overlapped        = nullptr;
    DWORD        ms_timeout        = poll_timeout == 0 ? INFINITE : static_cast<DWORD>(poll_timeout);
    BOOL         result            = GetQueuedCompletionStatus(iocp_handle_, &bytes_transferred, &completion_key,
                                                               &overlapped, ms_timeout);
    if (shut_.load(std::memory_order_acquire)) { return; }

    if (!result) {
      switch (DWORD err = GetLastError()) {
      case ERROR_ABANDONED_WAIT_0: // iocp 失效
        if (on_err_func_ != nullptr) { on_err_func_(this, ERROR_ABANDONED_WAIT_0); }
        break;
      default: // connection失效
        if (overlapped != nullptr) {
          OverlappedContext* context = CONTAINING_RECORD(overlapped, OverlappedContext, ol);
          if (context != nullptr && context->channel != nullptr) {
            context->channel->handle_completion_error(overlapped, err);
          }
        }
        break;
      }

      return;
    }

    if (overlapped != nullptr) {
      OverlappedContext* context = CONTAINING_RECORD(overlapped, OverlappedContext, ol);
      if (context != nullptr && context->channel != nullptr) {
        if (bytes_transferred == 0) {
          context->channel->handle_completion_error(overlapped, bytes_transferred);
        } else {
          context->channel->handle_completion(overlapped, bytes_transferred);
        }
      }
    }
  }
} // namespace iocpnet