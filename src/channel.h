#pragma once

#include <cstdint>
#include <deque>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

namespace fasterswiper {

// A bounded, multi-producer/multi-consumer channel for asynchronously passing
// items between threads.
//
// The channel has a configurable maximum queue depth. Writers will block when
// the queue is full, and readers will block when the queue is empty.
//
// The reader and writer sides can be closed independently:
//   - If the reader side is closed (via CloseReader()), the queue is cleared
//     and all current and future writes will fail with CANCELLED.
//   - If the writer side is closed (via CloseWriter()), the reader continues
//     to drain remaining items. Once the queue is empty, further reads will
//     fail with FAILED_PRECONDITION.
//
// Example:
//   Channel<int> channel(/*capacity=*/16);
//
//   // Writer thread:
//   channel.Write(42);
//   channel.CloseWriter();
//
//   // Reader thread:
//   absl::StatusOr<int> value = channel.Read();
//
template <typename T> class Channel {
public:
  // Creates a channel with the given maximum capacity. `capacity` must be > 0.
  explicit Channel(int64_t capacity) : capacity_(capacity) {}

  ~Channel() = default;

  // Non-copyable, non-movable.
  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;
  Channel(Channel &&) = delete;
  Channel &operator=(Channel &&) = delete;

  // Writes a value to the channel. Blocks if the queue is at capacity until
  // space becomes available, the reader side is closed, or the writer side is
  // closed.
  //
  // Returns OK on success.
  // Returns CANCELLED if the reader side has been closed.
  // Returns FAILED_PRECONDITION if the writer side has been closed.
  absl::Status Write(T value) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    mutex_.Await(absl::Condition(this, &Channel::CanWrite));

    if (reader_closed_) {
      return absl::CancelledError("Channel reader is closed");
    }
    if (writer_closed_) {
      return absl::FailedPreconditionError("Channel writer is closed");
    }

    queue_.push_back(std::move(value));
    return absl::OkStatus();
  }

  // Reads a value from the channel. Blocks if the queue is empty until an
  // item becomes available, the reader side is closed, or the writer side is
  // closed with an empty queue.
  //
  // Returns the next value on success.
  // Returns CANCELLED if the reader side has been closed.
  // Returns FAILED_PRECONDITION if the writer side has been closed and the
  //   queue is empty.
  absl::StatusOr<T> Read() ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    mutex_.Await(absl::Condition(this, &Channel::CanRead));

    if (!queue_.empty()) {
      T value = std::move(queue_.front());
      queue_.pop_front();
      return value;
    }

    if (reader_closed_) {
      return absl::CancelledError("Channel reader is closed");
    }

    // Writer is closed and queue is empty.
    return absl::FailedPreconditionError(
        "Channel writer is closed and queue is empty");
  }

  // Closes the writer side of the channel. After this call, no more values
  // can be written. The reader will continue to drain any remaining items;
  // once the queue is empty, reads will fail.
  //
  // Safe to call multiple times; subsequent calls are no-ops.
  void CloseWriter() ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    writer_closed_ = true;
  }

  // Closes the reader side of the channel. The queue is immediately cleared,
  // and all current and future writes will fail.
  //
  // Safe to call multiple times; subsequent calls are no-ops.
  void CloseReader() ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    reader_closed_ = true;
    queue_.clear();
  }

private:
  const int64_t capacity_;

  absl::Mutex mutex_;
  std::deque<T> queue_ ABSL_GUARDED_BY(mutex_);
  bool writer_closed_ ABSL_GUARDED_BY(mutex_) = false;
  bool reader_closed_ ABSL_GUARDED_BY(mutex_) = false;

  // Condition: the write can proceed (space available, or a close has
  // occurred that will cause Write() to return an error).
  bool CanWrite() const ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    return static_cast<int64_t>(queue_.size()) < capacity_ || reader_closed_ ||
           writer_closed_;
  }

  // Condition: the read can proceed (item available, or a close has occurred
  // that will cause Read() to return an error).
  bool CanRead() const ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    return !queue_.empty() || writer_closed_ || reader_closed_;
  }
};

} // namespace fasterswiper
