#pragma once

#include <stddef.h>

// Fixed-capacity ring buffer without heap allocation.
// When full, push() overwrites the oldest element. Not thread-safe; callers lock if needed.
template <typename T, size_t N>
class RingBuffer {
  static_assert(N > 0, "RingBuffer capacity must be > 0");

 public:
  void push(const T& item) {
    items_[(head_ + count_) % N] = item;
    if (count_ < N) {
      ++count_;
    } else {
      head_ = (head_ + 1) % N;
      ++overwritten_;
    }
  }

  // Removes and returns the oldest element. Returns false if empty.
  bool pop(T& out) {
    if (count_ == 0) return false;
    out = items_[head_];
    head_ = (head_ + 1) % N;
    --count_;
    return true;
  }

  // Element by age: 0 = oldest, size()-1 = newest.
  const T& at(size_t index) const { return items_[(head_ + index) % N]; }

  void clear() {
    head_ = 0;
    count_ = 0;
  }

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == N; }
  static constexpr size_t capacity() { return N; }
  // Number of elements lost by overwriting since construction.
  size_t overwritten() const { return overwritten_; }

 private:
  T items_[N] = {};
  size_t head_ = 0;
  size_t count_ = 0;
  size_t overwritten_ = 0;
};
