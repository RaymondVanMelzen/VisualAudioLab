#include "AudioAllocationProbe.h"
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace allocationProbe
{
thread_local bool enabled{};
thread_local std::uint64_t allocations{}, deallocations{};
}

void* operator new(std::size_t size)
{
    if (allocationProbe::enabled) ++allocationProbe::allocations;
    if (auto* data = std::malloc(size ? size : 1)) return data;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* data) noexcept
{
    if (data && allocationProbe::enabled) ++allocationProbe::deallocations;
    std::free(data);
}
void operator delete[](void* data) noexcept { ::operator delete(data); }
void operator delete(void* data, std::size_t) noexcept { ::operator delete(data); }
void operator delete[](void* data, std::size_t) noexcept { ::operator delete(data); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (allocationProbe::enabled) ++allocationProbe::allocations;
    const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    auto* data = _aligned_malloc(size ? size : 1, align);
#else
    auto* data = std::aligned_alloc(align, ((size ? size : 1) + align - 1) / align * align);
#endif
    if (data) return data;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* data, std::align_val_t) noexcept
{
    if (data && allocationProbe::enabled) ++allocationProbe::deallocations;
#if defined(_WIN32)
    _aligned_free(data);
#else
    std::free(data);
#endif
}
void operator delete[](void* data, std::align_val_t alignment) noexcept { ::operator delete(data, alignment); }
void operator delete(void* data, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(data, alignment); }
void operator delete[](void* data, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(data, alignment); }
