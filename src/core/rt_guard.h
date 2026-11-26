#pragma once

#include <cstdint>

// Realtime allocation guard (PLAN.md §6.2). In builds with PHOS_RT_GUARD (Debug), the global
// operator new/delete are replaced; any allocation or deallocation on a thread that is inside an
// rt::Scope counts as a violation and, by default, aborts with a message on stderr.
namespace phos::rt {

// True when the guard is compiled in (Debug builds).
bool guard_enabled() noexcept;

// Violations seen since start (all threads).
std::uint64_t violations() noexcept;

// Abort on the first violation (default true). Tests turn it off to observe the counter.
void set_abort_on_violation(bool abort) noexcept;

// Marks the current thread as realtime for the lifetime of the scope. Nestable.
class Scope {
public:
    Scope() noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// True while the current thread is inside an rt::Scope.
bool in_realtime() noexcept;

} // namespace phos::rt
