#pragma once

#include <atomic>

namespace phos::engine {

// Lock-free running max / min of a float shared with the UI (relaxed: meters only).
inline void store_max(std::atomic<float>& a, float v) noexcept {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

inline void store_min(std::atomic<float>& a, float v) noexcept {
    float cur = a.load(std::memory_order_relaxed);
    while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

} // namespace phos::engine
