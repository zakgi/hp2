// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Giammarco Zacheo

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace hp2 {

consteval std::size_t MinQueueLog2(std::size_t min_capacity) {
  return std::bit_width(min_capacity - 1);
}

// Lock-free single-producer / single-consumer ring buffer.
//
// One thread calls Push (producer), another thread calls Peek/Advance/Count
// (consumer). Capacity is 2^kCapacityLog2 so the wrap math reduces to a
// bitmask. The read and write cursors are 32-bit unsigned counters: 32-bit
// is chosen deliberately so embedded targets (Cortex-M, RV32, AVR32, etc.)
// can use a single-word atomic LDREX/STREX rather than the library-call
// fallback needed for 64-bit atomics. The counters wrap mod 2^32, and that
// is fine for correctness: because kCapacityLog2 < 32 we have
// kCapacity < 2^32, and the invariant (write - read) <= kCapacity holds at
// all times, so the unsigned subtraction always yields the correct delta
// regardless of how many times either cursor has wrapped. The only cost of
// the wrap is that external observers cannot treat the raw cursor values
// as strictly monotonic across a multi-day quiet period — a price we are
// willing to pay for the embedded-target performance win.
//
// Consumer semantics are designed for zero-copy drains. Peek() returns a
// std::span that points directly into the underlying storage and stops at
// the physical end of the array; a logical read that crosses the wrap is
// seen as two successive Peek/Advance cycles. The consumer processes the
// span, then calls Advance(n) to release those n slots back to the
// producer. Until Advance is called, the producer is forbidden by the
// fullness check from overwriting the region the span points at — this is
// what makes the zero-copy view safe under concurrent Push.
//
// Typical consumer idiom:
//     auto chunk = queue.Peek();
//     for (auto& x : chunk) { process(x); }
//     queue.Advance(chunk.size());
//
// Advance(n) must not exceed the count returned by the most recent Peek();
// violating that rolls read_ past write_ and corrupts the queue. No check
// is performed on the hot path.
//
// The two atomic cursors and the storage array are each pinned to their
// own cache line via alignas(64). This kills false sharing on hosts with
// MESI-style caches — producer writes to `write_` or to `storage_[w]` do
// not invalidate the line holding `read_` on the consumer CPU (or vice
// versa). On MCUs without cache coherency the alignment costs a little
// padding and is otherwise a no-op.
template <typename T, std::size_t kCapacityLog2>
class SpscQueue {
  static constexpr auto kMinSizeLog = std::size_t{0};
  static constexpr auto kMaxSizeLog = std::size_t{32};
  static constexpr auto kAlignment = std::size_t{64};

  static_assert(kCapacityLog2 > kMinSizeLog and kCapacityLog2 < kMaxSizeLog, "kCapacityLog2 out of bounds");
  // T must be trivially copyable. Push() does a plain assignment into the
  // ring slot and Peek() hands out a span the consumer reads from in place;
  // both rely on the slot being safely byte-copyable without running user
  // code (no copy ctor side effects, no throwing assignment, no destructor
  // ordering hazards on overwrite). Storage is value-initialized once at
  // construction, after which slots are reused by overwrite -- a non-
  // trivial T would expect destruction on every overwrite, which the queue
  // deliberately does not do. Embedded targets also benefit: trivially
  // copyable T survives memcpy, which is what a future bulk-push variant
  // will use.
  static_assert(std::is_trivially_copyable_v<T>, "SpscQueue<T>: T must be trivially copyable");
  static constexpr std::size_t kCapacity = std::size_t{1} << kCapacityLog2;
  static constexpr std::size_t kMask = kCapacity - 1;

 public:
  // Producer. Returns false if the queue is full; the caller decides
  // whether to drop, coalesce, or back off. Only the producer thread may
  // call this.
  bool Push(const T& value) {
    // relaxed on our own cursor -- no other thread writes it.
    const auto write = write_.load(std::memory_order_relaxed);
    // acquire on the peer cursor -- pairs with the consumer's release in
    // Advance() so anything the consumer released before updating read_
    // is visible, and, more importantly, so that the consumer's read of
    // any slot we are about to overwrite happens-before our write to it.
    const auto read = read_.load(std::memory_order_acquire);
    if (write - read >= kCapacity) {
      return false;
    }
    storage_[write & kMask] = value;
    // release publishes the slot write to the consumer.
    write_.store(write + 1, std::memory_order_release);
    return true;
  }

  // Producer. Bulk variant of Push: copies as much of `values` as the
  // queue currently has room for and publishes the batch with a single
  // release store of write_. The wrap across the physical end of the
  // storage array is handled internally with at most two element-wise
  // copies, so a batch of N elements costs N copies plus 2 atomic ops
  // -- compared to looping over Push() which would be N atomic ops on
  // each cursor and would also bounce the read_ cache line on every
  // iteration through the acquire load in Push. For audio sources that
  // produce frames in contiguous batches (a mixer, a resampler output
  // buffer) this is the preferred entry point.
  //
  // The copies use std::copy_n. T is statically constrained to be
  // trivially copyable, so libstdc++ / libc++ / MSVC all dispatch
  // copy_n on contiguous iterators to memmove -- identical codegen to
  // a hand-rolled memcpy, with no byte arithmetic at the call site.
  //
  // Returns the count actually accepted, in [0, values.size()]. A short
  // accept means the queue ran out of room; the caller can resubmit the
  // tail via values.subspan(returned). Only the producer thread may
  // call this.
  std::size_t PushBulk(std::span<const T> values) {
    const auto write = write_.load(std::memory_order_relaxed);
    // acquire on read_: see the data-race argument in Push() -- the
    // synchronizes-with edge is what stops the producer from racing
    // with a consumer that is still reading a slot we are about to
    // overwrite.
    const auto read = read_.load(std::memory_order_acquire);
    const auto take = std::min(values.size(), kCapacity - static_cast<std::size_t>(write - read));
    if (take > 0) {
      const auto start = static_cast<std::size_t>(write & kMask);
      const auto first_chunk = std::min(take, kCapacity - start);
      std::copy_n(values.data(), first_chunk, storage_.data() + start);
      if (take > first_chunk) {
        std::copy_n(values.data() + first_chunk, take - first_chunk, storage_.data());
      }
      // release publishes every slot in the batch in one go.
      write_.store(write + static_cast<std::uint32_t>(take), std::memory_order_release);
    }
    return take;
  }

  // Producer. Returns a span of up to `count` contiguous free slots at the
  // write head, without advancing write_. The span stops at the physical
  // end of the storage array, so a logical reservation that crosses the
  // wrap surfaces as two Reserve/Commit cycles -- the same shape as
  // Peek/Advance on the consumer side. An empty span means the queue is
  // full. After filling, the producer calls Commit(n) to publish n slots
  // with a single release store. Only the producer thread may call this.
  [[nodiscard]] std::span<T> Reserve(std::size_t count) {
    const auto write = write_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    const auto free = kCapacity - static_cast<std::size_t>(write - read);
    const auto take = std::min(count, free);
    if (take == 0) {
      return {};
    }
    const auto start = static_cast<std::size_t>(write & kMask);
    const auto chunk = std::min(take, kCapacity - start);
    return std::span<T>(storage_.data() + start, chunk);
  }

  // Producer. Publishes `n` slots previously returned by Reserve(). `n`
  // must be <= the length of the most recent Reserve()'s returned span;
  // no bounds check on the hot path. Commit(0) is a safe no-op. Only
  // the producer thread may call this.
  void Commit(std::size_t count) {
    if (count == 0) {
      return;
    }
    const auto write = write_.load(std::memory_order_relaxed);
    write_.store(write + static_cast<std::uint32_t>(count), std::memory_order_release);
  }

  // Consumer. Returns a single element if available.
  // Does NOT advance the read cursor -- the span remains
  // valid until the consumer calls Advance.
  [[nodiscard]] std::span<const T> PeekOne() const {
    const auto read = read_.load(std::memory_order_relaxed);
    // acquire pairs with the producer's release on write_ -- ensures the
    // slot contents we're about to read are visible on this CPU.
    const auto write = write_.load(std::memory_order_acquire);
    const auto available = static_cast<std::size_t>(write - read);
    if (available == 0) {
      return {};
    }
    const auto start = static_cast<std::size_t>(read & kMask);
    return std::span<const T>(storage_.data() + start, 1);
  }

  // Consumer. Returns a contiguous span of readable elements stopping at
  // the physical end of the underlying array. A logical read that wraps
  // surfaces across two Peek/Advance cycles. An empty span means there is
  // no data right now. Does NOT advance the read cursor — the span remains
  // valid until the consumer calls Advance.
  [[nodiscard]] std::span<const T> Peek() const {
    const auto read = read_.load(std::memory_order_relaxed);
    // acquire pairs with the producer's release on write_ -- ensures the
    // slot contents we're about to read are visible on this CPU.
    const auto write = write_.load(std::memory_order_acquire);
    const auto available = static_cast<std::size_t>(write - read);
    if (available == 0) {
      return {};
    }
    const auto start = static_cast<std::size_t>(read & kMask);
    // Clip to the physical tail; the caller is expected to Peek() again
    // for the wrapped suffix after Advance()ing past the first chunk.
    const auto chunk = std::min(available, kCapacity - start);
    return std::span<const T>(storage_.data() + start, chunk);
  }

  // Consumer. Fuses Peek + copy + Advance into a single call: copies
  // up to `dst.size()` elements out of the queue into the caller's
  // buffer and releases those slots back to the producer with one
  // release store of read_. The wrap across the physical end of the
  // storage array is handled internally with at most two element-wise
  // copies, so the caller never has to drive the two-Peek/Advance
  // cycle that the zero-copy Peek path requires. A `dst` of size 1 is
  // a degenerate single-element pop -- there is no separate PopOne.
  //
  // Returns the prefix of `dst` that was actually filled. An empty
  // return means the queue was empty; a full-size return means the
  // queue had at least dst.size() elements and dst is fully populated.
  // The functional-style return composes more cleanly than mutating
  // dst in place (the caller's variable is never silently resized) and
  // mirrors what std::ranges::copy returns.
  //
  // Only the consumer thread may call this.
  std::span<T> Pop(std::span<T> dst) {
    const auto read = read_.load(std::memory_order_relaxed);
    // acquire on write_: pairs with the producer's release on Push /
    // PushBulk so the slot contents we are about to copy are visible
    // on this CPU.
    const auto write = write_.load(std::memory_order_acquire);
    const auto take = std::min(dst.size(), static_cast<std::size_t>(write - read));
    if (take > 0) {
      const auto start = static_cast<std::size_t>(read & kMask);
      const auto first_chunk = std::min(take, kCapacity - start);
      std::copy_n(storage_.data() + start, first_chunk, dst.data());
      if (take > first_chunk) {
        std::copy_n(storage_.data(), take - first_chunk, dst.data() + first_chunk);
      }
      // release publishes the freed slots to the producer in one go.
      read_.store(read + static_cast<std::uint32_t>(take), std::memory_order_release);
    }
    return dst.first(take);
  }

  // Consumer. Release `n` previously-peeked slots back to the producer.
  // `n` must be <= the size of the most recent Peek(); no bounds check is
  // performed on the hot path. Calling Advance(0) is a safe no-op.
  void Advance(std::size_t n) {
    if (n == 0) {
      return;
    }
    const auto read = read_.load(std::memory_order_relaxed);
    // release publishes the consumer's advance so the producer sees the
    // slots as reusable, and synchronises with the producer's acquire in
    // Push().
    read_.store(read + n, std::memory_order_release);
  }

  // Total readable element count — write_ - read_. Safe to call from
  // either thread; intended mostly for diagnostics and tests. The value
  // is a momentary snapshot.
  std::size_t Count() const {
    const auto read = read_.load(std::memory_order_relaxed);
    const auto write = write_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(write - read);
  }

  static constexpr std::size_t Capacity() { return kCapacity; }

 private:
  // Each data member gets its own 64-byte-aligned slot so producer and
  // consumer never contend for the same cache line. 64 bytes is a
  // conservative line size (Intel/ARM big cores); on microarches with
  // smaller lines the alignment is simply wider than needed, not wrong.
  alignas(kAlignment) std::atomic<std::uint32_t> write_{0};
  alignas(kAlignment) std::atomic<std::uint32_t> read_{0};
  alignas(kAlignment) std::array<T, kCapacity> storage_{};
};

}  // namespace hp2
