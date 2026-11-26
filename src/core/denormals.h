#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define PHOS_FTZ_X86 1
#elif defined(__aarch64__) && !defined(_MSC_VER)
#define PHOS_FTZ_ARM64 1
#endif

namespace phos {

// Flush-to-zero / denormals-are-zero for the lifetime of the scope (PLAN.md §6.2), restoring the
// previous mode on exit because the audio thread belongs to the OS. Denormals make IIR tails and
// reverbs crawl; with FTZ they become exact zeros.
//   x86:   MXCSR bit 15 (FTZ) and bit 6 (DAZ).
//   arm64: FPCR bit 24 (FZ) flushes both denormal inputs and results.
class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept {
#if defined(PHOS_FTZ_X86)
        old_ = _mm_getcsr();
        _mm_setcsr(old_ | 0x8040u);
#elif defined(PHOS_FTZ_ARM64)
        std::uint64_t v;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(v));
        old_ = v;
        v |= (std::uint64_t{1} << 24);
        __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
#endif
    }
    ~ScopedFlushDenormals() {
#if defined(PHOS_FTZ_X86)
        _mm_setcsr(static_cast<unsigned>(old_));
#elif defined(PHOS_FTZ_ARM64)
        const std::uint64_t v = old_;
        __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

    static constexpr bool supported() {
#if defined(PHOS_FTZ_X86) || defined(PHOS_FTZ_ARM64)
        return true;
#else
        return false;
#endif
    }

private:
    [[maybe_unused]] std::uint64_t old_ = 0;
};

} // namespace phos
