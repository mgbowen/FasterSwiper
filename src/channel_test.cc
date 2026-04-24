#include "src/channel.h"

#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace fasterswiper {
namespace {

TEST(ChannelTest, SingleWriteAndRead) {
  Channel<int> channel(/*capacity=*/1);
  ASSERT_TRUE(channel.Write(42).ok());
  absl::StatusOr<int> result = channel.Read();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, 42);
}

TEST(ChannelTest, FifoOrdering) {
  Channel<int> channel(/*capacity=*/8);
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(channel.Write(i).ok());
  }
  for (int i = 0; i < 5; ++i) {
    absl::StatusOr<int> result = channel.Read();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, i);
  }
}

TEST(ChannelTest, CloseWriterDrainsQueue) {
  Channel<int> channel(/*capacity=*/8);
  ASSERT_TRUE(channel.Write(1).ok());
  ASSERT_TRUE(channel.Write(2).ok());
  ASSERT_TRUE(channel.Write(3).ok());
  channel.CloseWriter();

  // Should still be able to read all queued items.
  for (int expected = 1; expected <= 3; ++expected) {
    absl::StatusOr<int> result = channel.Read();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, expected);
  }

  // Now that the queue is empty and writer is closed, reads should fail.
  absl::StatusOr<int> result = channel.Read();
  ASSERT_FALSE(result.ok());
  EXPECT_TRUE(absl::IsFailedPrecondition(result.status()));
}

TEST(ChannelTest, CloseWriterRejectsNewWrites) {
  Channel<int> channel(/*capacity=*/8);
  channel.CloseWriter();
  absl::Status status = channel.Write(1);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::IsFailedPrecondition(status));
}

TEST(ChannelTest, CloseReaderClearsQueueAndRejectsWrites) {
  Channel<int> channel(/*capacity=*/8);
  ASSERT_TRUE(channel.Write(1).ok());
  ASSERT_TRUE(channel.Write(2).ok());
  channel.CloseReader();

  // Writes should now fail with CANCELLED.
  absl::Status status = channel.Write(3);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::IsCancelled(status));

  // Reads should also fail with CANCELLED.
  absl::StatusOr<int> result = channel.Read();
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::IsCancelled(result.status()));
}

TEST(ChannelTest, CloseReaderUnblocksWriter) {
  Channel<int> channel(/*capacity=*/1);
  ASSERT_TRUE(channel.Write(1).ok());

  // This write will block because the channel is full.
  absl::Notification done;
  std::thread writer([&] {
    absl::Status status = channel.Write(2);
    EXPECT_TRUE(absl::IsCancelled(status));
    done.Notify();
  });

  // Give the writer time to block, then close the reader.
  absl::SleepFor(absl::Milliseconds(50));
  channel.CloseReader();

  done.WaitForNotification();
  writer.join();
}

TEST(ChannelTest, CloseWriterUnblocksReader) {
  Channel<int> channel(/*capacity=*/1);

  absl::Notification done;
  std::thread reader([&] {
    absl::StatusOr<int> result = channel.Read();
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsFailedPrecondition(result.status()));
    done.Notify();
  });

  // Give the reader time to block, then close the writer.
  absl::SleepFor(absl::Milliseconds(50));
  channel.CloseWriter();

  done.WaitForNotification();
  reader.join();
}

TEST(ChannelTest, WriteBlocksWhenFull) {
  Channel<int> channel(/*capacity=*/2);
  ASSERT_TRUE(channel.Write(1).ok());
  ASSERT_TRUE(channel.Write(2).ok());

  // Third write should block. Verify it does by using a thread.
  absl::Notification write_started;
  absl::Notification write_finished;
  std::thread writer([&] {
    write_started.Notify();
    absl::Status status = channel.Write(3);
    EXPECT_TRUE(status.ok());
    write_finished.Notify();
  });

  write_started.WaitForNotification();
  // Writer should be blocked; the write_finished notification should NOT fire
  // within a short window.
  absl::SleepFor(absl::Milliseconds(50));
  EXPECT_FALSE(write_finished.HasBeenNotified());

  // Now consume an item, which should unblock the writer.
  absl::StatusOr<int> result = channel.Read();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, 1);

  write_finished.WaitForNotification();
  writer.join();

  // Drain the remaining items.
  result = channel.Read();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, 2);

  result = channel.Read();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, 3);
}

TEST(ChannelTest, ReadBlocksWhenEmpty) {
  Channel<int> channel(/*capacity=*/2);

  absl::Notification read_started;
  absl::Notification read_finished;
  absl::StatusOr<int> read_value;

  std::thread reader([&] {
    read_started.Notify();
    read_value = channel.Read();
    read_finished.Notify();
  });

  read_started.WaitForNotification();
  absl::SleepFor(absl::Milliseconds(50));
  EXPECT_FALSE(read_finished.HasBeenNotified());

  // Write an item, which should unblock the reader.
  ASSERT_TRUE(channel.Write(99).ok());
  read_finished.WaitForNotification();
  reader.join();

  ASSERT_TRUE(read_value.ok());
  EXPECT_EQ(*read_value, 99);
}

TEST(ChannelTest, MultipleProducersMultipleConsumers) {
  constexpr int kNumProducers = 4;
  constexpr int kItemsPerProducer = 100;
  constexpr int kNumConsumers = 4;

  Channel<int> channel(/*capacity=*/16);
  std::atomic<int> total_consumed{0};

  // Start consumers.
  std::vector<std::thread> consumers;
  for (int i = 0; i < kNumConsumers; ++i) {
    consumers.emplace_back([&] {
      while (true) {
        absl::StatusOr<int> result = channel.Read();
        if (!result.ok()) break;
        total_consumed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Start producers.
  std::vector<std::thread> producers;
  for (int i = 0; i < kNumProducers; ++i) {
    producers.emplace_back([&, i] {
      for (int j = 0; j < kItemsPerProducer; ++j) {
        absl::Status status = channel.Write(i * kItemsPerProducer + j);
        ASSERT_TRUE(status.ok());
      }
    });
  }

  for (auto &t : producers) t.join();
  channel.CloseWriter();
  for (auto &t : consumers) t.join();

  EXPECT_EQ(total_consumed.load(), kNumProducers * kItemsPerProducer);
}

TEST(ChannelTest, MoveOnlyTypes) {
  Channel<std::unique_ptr<int>> channel(/*capacity=*/4);
  ASSERT_TRUE(channel.Write(std::make_unique<int>(7)).ok());

  absl::StatusOr<std::unique_ptr<int>> result = channel.Read();
  ASSERT_TRUE(result.ok());
  ASSERT_NE(*result, nullptr);
  EXPECT_EQ(**result, 7);
}

TEST(ChannelTest, CloseReaderThenCloseWriter) {
  Channel<int> channel(/*capacity=*/4);
  channel.CloseReader();
  channel.CloseWriter();

  // Both reads and writes should fail.
  EXPECT_TRUE(absl::IsCancelled(channel.Write(1)));
  EXPECT_TRUE(absl::IsCancelled(channel.Read().status()));
}

TEST(ChannelTest, CloseWriterThenCloseReader) {
  Channel<int> channel(/*capacity=*/4);
  ASSERT_TRUE(channel.Write(1).ok());
  channel.CloseWriter();
  channel.CloseReader();

  // Reader closed takes priority, so both should fail with CANCELLED.
  EXPECT_TRUE(absl::IsCancelled(channel.Write(1)));
  EXPECT_TRUE(absl::IsCancelled(channel.Read().status()));
}

}  // namespace
}  // namespace fasterswiper
