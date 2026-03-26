#include "iocp_event_poll.h"
#include "iocp_channel.h"
#include "iocp_context.h"

#include <iostream>

namespace iocpnet {
  IOCPEventPoll::IOCPEventPoll(HANDLE handle)
      : iocp_handle_ {handle}
      , shut_ {false}
      , on_err_func_ {nullptr}
      , timer_manager_ {std::make_unique<TimerManager>()} {
    thread_id_ = std::this_thread::get_id();
  }

  IOCPEventPoll::~IOCPEventPoll() {
    // IMPORTANT: Shut down timer manager FIRST before calling shutdown()
    // This prevents timer callbacks from accessing partially destroyed objects
    if (timer_manager_) {
      timer_manager_->shutdown();
    }

    // Now it's safe to call shutdown() which posts to IOCP
    // No timer callbacks will be running at this point
    shutdown();

    // Clear any callbacks that might hold references
    on_err_func_ = nullptr;

    // Clear object pools to ensure no dangling references
    // This is safe because all connections should be destroyed before this
    accept_pool_.clear();
    read_pool_.clear();
    write_pool_.clear();
  }

  void IOCPEventPoll::shutdown() {
    if (shut_.exchange(true, std::memory_order_acq_rel)) { return; }

    PostQueuedCompletionStatus(iocp_handle_, 0, 0, nullptr);
  }

  bool IOCPEventPoll::register_in(socket_t handle, IOCPChannel* channel) {
    if (channel == nullptr) { return false; }

    HANDLE result = CreateIoCompletionPort(reinterpret_cast<HANDLE>(handle), iocp_handle_,
                                           reinterpret_cast<ULONG_PTR>(channel), 0);

    return result != nullptr && result == iocp_handle_;
  }

  void IOCPEventPoll::poll() {
    if (shut_.load(std::memory_order_acquire)) { return; }
    poll_(kPollTimeoutMS);
  }

  void IOCPEventPoll::run() {
    thread_id_ = std::this_thread::get_id();
    while (!shut_.load(std::memory_order_acquire)) {
      poll_(kInfiniteTimeout); // 无限阻塞
    }
  }

  bool IOCPEventPoll::process_one_event_(uint32_t poll_timeout) {
    DWORD        bytes_transferred = 0;
    ULONG_PTR    completion_key    = 0;
    LPOVERLAPPED overlapped        = nullptr;
    // poll_timeout == 0 表示非阻塞，UINT32_MAX 表示无限阻塞
    DWORD ms_timeout = (poll_timeout == UINT32_MAX) ? INFINITE : static_cast<DWORD>(poll_timeout);

    // 获取完成状态
    BOOL result = GetQueuedCompletionStatus(iocp_handle_, &bytes_transferred, &completion_key, &overlapped, ms_timeout);
    if (shut_.load(std::memory_order_acquire)) { return false; }

    IOCPChannel* channel = reinterpret_cast<IOCPChannel*>(completion_key);

    if (!result) {
      DWORD err = GetLastError(); // GetQueuedCompletionStatus 失败时使用 GetLastError()
      if (err == WAIT_TIMEOUT) { return false; }
      if (err == ERROR_ABANDONED_WAIT_0) {
        // IOCP 句柄失效
        if (on_err_func_ != nullptr) { on_err_func_(this, ERROR_ABANDONED_WAIT_0); }
        return false;
      }

      // 如果有 handler 且出错，通知 handler 处理
      if (channel != nullptr && overlapped != nullptr) {
        channel->handle_completion_error(overlapped, err);
      }
      return false;
    }

    if (channel != nullptr && overlapped != nullptr) {
      channel->handle_completion(overlapped, bytes_transferred);
    }

    return true;
  }

  void IOCPEventPoll::poll_(uint32_t poll_timeout) {
    // 首次使用指定超时等待
    if (!process_one_event_(poll_timeout)) { return; }

    // 批量处理后续事件（非阻塞：poll_timeout=0）
    uint32_t processed = 1;
    while (processed < kMaxBatchSize && !shut_.load(std::memory_order_acquire)) {
      if (!process_one_event_(0)) { break; } // 非阻塞，无事件则退出
      ++processed;
    }
  }
} // namespace iocpnet
