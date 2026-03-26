#ifndef BUFFER_H
#define BUFFER_H

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>

namespace iocpnet {
  class Buffer {
    static constexpr size_t kInitialCapacity = 8192;

  public:
    Buffer(size_t initial_capacity = kInitialCapacity) {
      capacity_ = (std::max)(initial_capacity, kInitialCapacity);
      data_     = new char[capacity_];
    }

    Buffer(const Buffer&)            = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(const char* data, size_t size) noexcept {
      data_ = new char[size];
      memcpy(data_, data, size);
      capacity_    = size;
      write_index_ = size;
      read_index_  = 0;
    }

    Buffer(Buffer&& other) noexcept {
      data_        = other.data_;
      read_index_  = other.read_index_;
      write_index_ = other.write_index_;
      capacity_    = other.capacity_;
      // 将原对象重置为有效但空的状态
      other.data_        = nullptr;
      other.read_index_  = 0;
      other.write_index_ = 0;
      other.capacity_    = 0;
    }
    Buffer& operator=(Buffer&& other) noexcept {
      if (data_ == other.data_) { return *this; }

      delete[] data_;
      data_        = other.data_;
      read_index_  = other.read_index_;
      write_index_ = other.write_index_;
      capacity_    = other.capacity_;
      // 将原对象重置为有效但空的状态
      other.data_        = nullptr;
      other.read_index_  = 0;
      other.write_index_ = 0;
      other.capacity_    = 0;
      return *this;
    }
    ~Buffer() { delete[] data_; }

    void   clear() { read_index_ = write_index_ = 0; }
    size_t readable_size() const { return write_index_ - read_index_; }
    size_t writable_size() const { return capacity_ - write_index_; }
    // read data, at the most time, use readable_size get data length
    char* to_read() const { return data_ + read_index_; }
    // if read in OnMessageCallback
    // must invoke been_read or retrieve tell Buffer your read length
    void been_read(size_t len) {
      assert(len <= readable_size() && "been_read: len exceeds readable size");
      read_index_ += len;
      shrink_if_needed_();
    }
    void been_read_all() {
      been_read(readable_size());
    }

  private:
    // 自动收缩：闲置空间超过容量 75% 且超过 16KB 时收缩
    void shrink_if_needed_() {
      size_t used = readable_size();
      size_t idle = capacity_ - used;

      // 闲置空间需同时满足：比例 > 75% 且绝对值 > 16KB
      if (idle * 4 <= capacity_ * 3 || idle < 16384) { return; }

      // 目标容量：取 (使用量, kInitialCapacity) 的较大值，预留 50% 空间
      size_t target = (std::max)(used, kInitialCapacity);
      target        = target + target / 2;

      char* new_buf = new char[target];
      std::memcpy(new_buf, data_ + read_index_, used);
      delete[] data_;

      data_        = new_buf;
      capacity_    = target;
      read_index_  = 0;
      write_index_ = used;
    }

  public:
    // write data
    char* to_write() const { return data_ + write_index_; }
    void  been_written(size_t len) {
      assert(len <= writable_size() && "been_written: len exceeds writable size");
      write_index_ += len;
    }

    void append(std::string_view data) { append(data.data(), data.size()); };
    void append(const char* data, size_t len) {
      ensure_writable_size(len);
      std::memcpy(to_write(), data, len);
      write_index_ += len;
    }

    void ensure_writable_size(size_t len) {
      size_t head_size = read_index_;
      size_t tail_size = writable_size();
      if (tail_size < len) {
        if (head_size + tail_size >= len) {
          size_t written_size = write_index_ - read_index_;
          std::memmove(data_, data_ + read_index_, written_size);
          read_index_  = 0;
          write_index_ = written_size;
        } else {
          size_t      written_size = readable_size();
          size_t      new_capacity = capacity_ * 2 + len;
          const char* pre          = to_read();
          char*       new_buffer   = new char[new_capacity];
          std::memcpy(new_buffer, pre, written_size);
          delete[] data_;

          data_        = new_buffer;
          capacity_    = new_capacity;
          read_index_  = 0;
          write_index_ = written_size;
        }
      }
    }
  private:
    char*  data_        = nullptr;
    size_t write_index_ = 0;
    size_t read_index_  = 0;
    size_t capacity_    = 0;
  };
} // namespace iocpnet

#endif // BUFFER_H
