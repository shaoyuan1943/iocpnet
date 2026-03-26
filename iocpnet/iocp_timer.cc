#include "iocp_timer.h"

namespace iocpnet {

  Timer::Timer(TimerID id, uint32_t delay_ms, Callback cb)
      : id_(id)
      , delay_ms_(delay_ms)
      , callback_(std::move(cb)) {
    expire_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
  }

  void Timer::execute() const {
    if (callback_ && !cancelled_.load(std::memory_order_acquire)) {
      callback_();
    }
  }

  TimerManager::TimerManager() {
    timer_thread_ = std::thread(&TimerManager::timer_thread_func_, this);
  }

  TimerManager::~TimerManager() { shutdown(); }

  Timer::TimerID TimerManager::add_timer(uint32_t delay_ms, Timer::Callback cb) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_.load(std::memory_order_acquire)) { return 0; }

    Timer::TimerID id    = next_id_++;
    auto           timer = std::make_unique<Timer>(id, delay_ms, std::move(cb));
    timers_[id]          = std::move(timer);

    cv_.notify_one();
    return id;
  }

  void TimerManager::cancel_timer(Timer::TimerID id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_.load(std::memory_order_acquire)) { return; }

    auto it = timers_.find(id);
    if (it != timers_.end()) {
      it->second->cancel();
      timers_.erase(it);
      cv_.notify_one();
    }
  }

  void TimerManager::shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) { return; }

    cv_.notify_all();

    if (timer_thread_.joinable()) {
      timer_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    timers_.clear();
  }

  void TimerManager::timer_thread_func_() {
    while (running_.load(std::memory_order_acquire)) {
      std::unique_lock<std::mutex> lock(mutex_);

      // 等待条件：有定时器或停止信号
      cv_.wait(lock, [this] {
        return !running_.load(std::memory_order_acquire) || !timers_.empty();
      });

      if (!running_.load(std::memory_order_acquire)) { break; }

      // 找到最近的到期时间
      auto now      = std::chrono::steady_clock::now();
      auto earliest = timers_.end();

      for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->second->cancelled()) { continue; }
        if (earliest == timers_.end() || it->second->expire_time_ < earliest->second->expire_time_) {
          earliest = it;
        }
      }

      if (earliest == timers_.end()) {
        // 清理已取消的定时器
        for (auto it = timers_.begin(); it != timers_.end();) {
          if (it->second->cancelled()) {
            it = timers_.erase(it);
          } else {
            ++it;
          }
        }
        continue;
      }

      // 等待直到最早定时器到期
      auto status = cv_.wait_until(lock, earliest->second->expire_time_);

      if (!running_.load(std::memory_order_acquire)) { break; }

      // 收集到期的定时器
      now = std::chrono::steady_clock::now();
      std::vector<std::unique_ptr<Timer>> expired_timers;

      for (auto it = timers_.begin(); it != timers_.end();) {
        if (it->second->cancelled()) {
          it = timers_.erase(it);
          continue;
        }
        if (it->second->expire_time_ <= now) {
          expired_timers.push_back(std::move(it->second));
          it = timers_.erase(it);
        } else {
          ++it;
        }
      }

      // 释放锁后再执行回调，避免死锁
      lock.unlock();

      for (auto& timer : expired_timers) {
        if (!running_.load(std::memory_order_acquire)) { break; }
        timer->execute();
      }
    }
  }

} // namespace iocpnet
