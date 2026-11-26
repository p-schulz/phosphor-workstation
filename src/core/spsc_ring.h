#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace phos {

// Wait-free single-producer / single-consumer ring of trivially copyable values. Storage is a
// fixed in-object array, so push/pop never allocate and are safe on the audio thread. Exactly one
// thread may push and exactly one (other) thread may pop.
template <typename T, std::size_t Capacity> class SpscRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "ring elements are copied with plain assignment");

public:
    static constexpr std::size_t capacity() { return Capacity; }

    // Producer. Returns false (and drops nothing) when full.
    bool push(const T& value) noexcept {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == Capacity) {
            return false;
        }
        slots_[w & kMask] = value;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    // Producer: free slots (exact for the producer, conservative otherwise).
    std::size_t free_space() const noexcept {
        return Capacity - (write_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire));
    }

    // Consumer. Returns false when empty.
    bool pop(T& out) noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) {
            return false;
        }
        out = slots_[r & kMask];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return read_.load(std::memory_order_acquire) == write_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
    alignas(64) std::array<T, Capacity> slots_{};
};

} // namespace phos
