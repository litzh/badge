#pragma once
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>
using TickType_t = uint32_t;
constexpr int pdTRUE = 1, pdFALSE = 0;
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline uint32_t millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct TestQueue {
  size_t length, bytes;
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<std::vector<char>> items;
  TestQueue(size_t n, size_t size): length(n), bytes(size) {}
};
using QueueHandle_t = TestQueue *;
inline QueueHandle_t xQueueCreate(size_t n, size_t bytes) { return new TestQueue(n, bytes); }
inline int xQueueReceive(QueueHandle_t q, void *out, TickType_t timeout) {
  std::unique_lock<std::mutex> lock(q->mutex);
  if (!q->changed.wait_for(lock, std::chrono::milliseconds(timeout), [&]{ return !q->items.empty(); })) return pdFALSE;
  memcpy(out, q->items.front().data(), q->bytes); q->items.pop_front(); return pdTRUE;
}
inline int xQueueSend(QueueHandle_t q, const void *item, TickType_t) {
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->items.size() >= q->length) return pdFALSE;
  q->items.emplace_back((const char *)item, (const char *)item + q->bytes); q->changed.notify_all(); return pdTRUE;
}
inline void xQueueOverwrite(QueueHandle_t q, const void *item) {
  std::lock_guard<std::mutex> lock(q->mutex);
  q->items.clear(); q->items.emplace_back((const char *)item, (const char *)item + q->bytes); q->changed.notify_all();
}
inline void xQueueReset(QueueHandle_t q) { std::lock_guard<std::mutex> lock(q->mutex); q->items.clear(); }
