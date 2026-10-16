#pragma once
// Single-producer / single-consumer ring for handing data from one task to another (BLE callback ->
// loop) without locks. N must be a power of two; all N slots are usable. No Arduino includes.
#include <atomic>
#include <stddef.h>
#include <stdint.h>

template <typename T, size_t N>
class SpscRing {
  static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two (indices wrap at 2^32)");

 public:
  // Producer task only. False when full: the item is dropped.
  bool push(const T& v) {
    uint32_t h = head_.load(std::memory_order_relaxed);
    uint32_t t = tail_.load(std::memory_order_acquire);
    if (h - t >= N) return false;
    buf_[h & (N - 1)] = v;                          // payload first ...
    head_.store(h + 1, std::memory_order_release);  // ... then publish
    return true;
  }

  // Consumer task only. False when empty.
  bool pop(T& out) {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    uint32_t h = head_.load(std::memory_order_acquire);
    if (t == h) return false;
    out = buf_[t & (N - 1)];
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }

 private:
  T buf_[N];
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
};
