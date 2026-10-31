#pragma once

#include <cstdint>

namespace allocationProbe
{
extern thread_local bool enabled;
extern thread_local std::uint64_t allocations, deallocations;

/** Counts C++ allocation/deallocation on the current callback thread only. */
class Scope final
{
public:
    Scope() { enabled = true; allocations = 0; deallocations = 0; }
    ~Scope() { enabled = false; }
};
}
