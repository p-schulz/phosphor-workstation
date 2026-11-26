#include "core/rt_guard.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace phos::rt {

namespace {

// Plain thread_local int: constant-initialised, so reading it inside operator new is safe on any
// thread at any time (no dynamic TLS initialisation that could itself allocate).
thread_local int t_depth = 0;
std::atomic<std::uint64_t> g_violations{0};
std::atomic<bool> g_abort{true};

} // namespace

bool guard_enabled() noexcept {
#if defined(PHOS_RT_GUARD)
    return true;
#else
    return false;
#endif
}

std::uint64_t violations() noexcept { return g_violations.load(std::memory_order_relaxed); }
void set_abort_on_violation(bool abort) noexcept { g_abort.store(abort, std::memory_order_relaxed); }
Scope::Scope() noexcept { ++t_depth; }
Scope::~Scope() { --t_depth; }
bool in_realtime() noexcept { return t_depth > 0; }

namespace detail {

inline void check(const char* what) noexcept {
    if (t_depth == 0) {
        return;
    }
    g_violations.fetch_add(1, std::memory_order_relaxed);
    if (g_abort.load(std::memory_order_relaxed)) {
        // Leaving the scope first lets stderr's own buffering allocate without recursing.
        t_depth = 0;
        std::fprintf(stderr, "rt_guard: %s on a realtime thread (PLAN.md 6.2)\n", what);
        std::abort();
    }
}

} // namespace detail

} // namespace phos::rt

#if defined(PHOS_RT_GUARD)

// ---------------------------------------------------------------------------------------------
// Replacement global allocation functions (Debug only). They forward to malloc/free and report
// any call made inside an rt::Scope.
// ---------------------------------------------------------------------------------------------
namespace {

void* guarded_alloc(std::size_t n) noexcept {
    phos::rt::detail::check("allocation");
    return std::malloc(n == 0 ? 1 : n);
}

void* guarded_alloc_aligned(std::size_t n, std::align_val_t al) noexcept {
    phos::rt::detail::check("allocation");
    const auto a = static_cast<std::size_t>(al);
    if (n == 0) {
        n = 1;
    }
#if defined(_WIN32)
    return _aligned_malloc(n, a);
#else
    void* p = nullptr;
    return posix_memalign(&p, a < sizeof(void*) ? sizeof(void*) : a, n) == 0 ? p : nullptr;
#endif
}

void guarded_free(void* p) noexcept {
    if (p) {
        phos::rt::detail::check("deallocation");
        std::free(p);
    }
}

void guarded_free_aligned(void* p) noexcept {
    if (p) {
        phos::rt::detail::check("deallocation");
#if defined(_WIN32)
        _aligned_free(p);
#else
        std::free(p);
#endif
    }
}

void* or_throw(void* p) {
    if (!p) {
        throw std::bad_alloc();
    }
    return p;
}

} // namespace

void* operator new(std::size_t n) { return or_throw(guarded_alloc(n)); }
void* operator new[](std::size_t n) { return or_throw(guarded_alloc(n)); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return guarded_alloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return guarded_alloc(n); }
void* operator new(std::size_t n, std::align_val_t a) { return or_throw(guarded_alloc_aligned(n, a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return or_throw(guarded_alloc_aligned(n, a)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return guarded_alloc_aligned(n, a);
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return guarded_alloc_aligned(n, a);
}

void operator delete(void* p) noexcept { guarded_free(p); }
void operator delete[](void* p) noexcept { guarded_free(p); }
void operator delete(void* p, std::size_t) noexcept { guarded_free(p); }
void operator delete[](void* p, std::size_t) noexcept { guarded_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { guarded_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { guarded_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { guarded_free_aligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { guarded_free_aligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { guarded_free_aligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { guarded_free_aligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { guarded_free_aligned(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { guarded_free_aligned(p); }

#endif // PHOS_RT_GUARD
