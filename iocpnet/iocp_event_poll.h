#ifndef IOCP_EVENT_LOOP_H
#define IOCP_EVENT_LOOP_H

#include "iocp_context.h"
#include "iocp_sock.h"
#include "iocp_timer.h"

#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace iocpnet {
  class IOCPChannel;

  template<typename T>
  class ContextPool : public NonCopyable {
  public:
    ContextPool() = default;
    ~ContextPool() { clear(); }

    template<typename... Args>
    T* acquire(Args&&... args) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (free_list_.empty()) {
        return new T(std::forward<Args>(args)...);
      }

      T* obj = free_list_.back();
      free_list_.pop_back();
      obj->~T();
      new (obj) T(std::forward<Args>(args)...);
      return obj;
    }

    void release(T* obj) {
      if (obj == nullptr) { return; }
      std::lock_guard<std::mutex> lock(mutex_);
      free_list_.push_back(obj);
    }

    void clear() {
      std::lock_guard<std::mutex> lock(mutex_);
      for (T* obj : free_list_) {
        delete obj;
      }
      free_list_.clear();
    }
  private:
    std::vector<T*> free_list_;
    std::mutex      mutex_;
  };

  class IOCPEventPoll : public NonCopyable {
  public:
    explicit IOCPEventPoll(HANDLE handle);
    ~IOCPEventPoll();

    void shutdown();
    bool register_in(socket_t handle, IOCPChannel* channel);
    void run();
    void poll();

    ContextPool<AcceptContext>& accept_pool() { return accept_pool_; }
    ContextPool<ReadContext>&   read_pool() { return read_pool_; }
    ContextPool<WriteContext>&  write_pool() { return write_pool_; }
    TimerManager*               timer_manager() const { return timer_manager_.get(); }

    void   set_name(std::string_view name) { name_ = name; }
    bool   is_in_poll_thread() const { return thread_id_ == std::this_thread::get_id(); }
    void   set_poll_error_callback(std::function<void(IOCPEventPoll*, DWORD)> func) { on_err_func_ = std::move(func); }
    HANDLE iocp_handle() const { return iocp_handle_; }
  private:
    static constexpr uint32_t kInfiniteTimeout = UINT32_MAX;
    static constexpr uint32_t kMaxBatchSize    = 64;

    void poll_(uint32_t poll_timeout = 0);
    bool process_one_event_(uint32_t poll_timeout);
  private:
    ContextPool<AcceptContext>                  accept_pool_;
    ContextPool<ReadContext>                    read_pool_;
    ContextPool<WriteContext>                   write_pool_;
    HANDLE                                      iocp_handle_;
    std::atomic<bool>                           shut_;
    std::thread::id                             thread_id_;
    std::string                                 name_;
    std::mutex                                  mutex_;
    std::function<void(IOCPEventPoll*, DWORD)>  on_err_func_;
    std::unique_ptr<TimerManager>               timer_manager_;
  };
} // namespace iocpnet

#endif
