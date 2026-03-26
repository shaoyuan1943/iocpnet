#ifndef IOCP_TIMER_H
#define IOCP_TIMER_H

#include "iocp_sock.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace iocpnet {

  class Timer;
  class TimerManager;

  class Timer : public NonCopyable {
  public:
    using TimerID  = uint64_t;
    using Callback = std::function<void()>;

    Timer(TimerID id, uint32_t delay_ms, Callback cb);
    ~Timer() = default;

    TimerID   id() const { return id_; }
    uint32_t  delay_ms() const { return delay_ms_; }
    bool      cancelled() const { return cancelled_.load(std::memory_order_acquire); }
    void      cancel() { cancelled_.store(true, std::memory_order_release); }
    void      execute() const;

    bool operator<(const Timer& other) const { return expire_time_ < other.expire_time_; }
  private:
    TimerID                               id_;
    uint32_t                              delay_ms_;
    Callback                              callback_;
    std::chrono::steady_clock::time_point expire_time_;
    std::atomic_bool                      cancelled_ {false};

    friend class TimerManager;
  };

  // 定时器管理器（单线程处理所有定时器）
  class TimerManager : public NonCopyable {
  public:
    TimerManager();
    ~TimerManager();

    // 添加一次性定时器，返回定时器 ID
    Timer::TimerID add_timer(uint32_t delay_ms, Timer::Callback cb);

    // 取消定时器
    void cancel_timer(Timer::TimerID id);

    // 关闭管理器（停止定时器线程）
    void shutdown();
  private:
    void timer_thread_func_();

    std::map<Timer::TimerID, std::unique_ptr<Timer>> timers_;
    std::mutex                                       mutex_;
    std::condition_variable                          cv_;
    std::thread                                      timer_thread_;
    std::atomic_bool                                 running_ {true};
    Timer::TimerID                                   next_id_ {1};
  };

} // namespace iocpnet

#endif // IOCP_TIMER_H