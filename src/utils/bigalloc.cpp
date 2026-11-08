// PumpkinSpice's operator new/delete: allocations of 256 KiB or more come from Aroma's mapped memory
// (MemoryMappingModule), the rest from the plugin heap. Rebuilding a theme package holds Menu archive members of several
// MB at once (up to 23 MB for one model in Men2.pack), more than the plugin heap has. Small blocks stay on the plugin
// heap: sending every allocation to mapped memory made Men.pack and Set2.pack fail on a US console too (2026-11-08),
// where they had worked with only the big blocks there. Any allocation that mapped memory can't serve uses the plugin
// heap, or while a package is rebuilt the Wii U Menu's own heap (AppHeapFallback, bigalloc.h): Men2.pack still ran out
// with a 23 MB model (2026-11-08).
// Every block starts with a header saying where it came from, so delete always frees it the right way.
#include "bigalloc.h"
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory/mappedmemory.h>
#include <new>

namespace {
    constexpr std::size_t kBig      = 256 * 1024;
    constexpr std::size_t kHeader   = 32;         // the returned pointer keeps 32-byte alignment (blocks are at least that)
    constexpr std::uint32_t kMapped = 0x4D415050; // "MAPP"
    constexpr std::uint32_t kHeap   = 0x48454150; // "HEAP"
    constexpr std::uint32_t kApp    = 0x41505048; // "APPH": the Wii U Menu's heap
    int sAppHeap                    = 0;          // > 0 while AppHeapFallback exists
    std::size_t sLastFailed         = 0;

    MEMHeapHandle AppHeap() {
        return MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);
    }

    void *Alloc(std::size_t size) noexcept {
        void *raw          = nullptr;
        std::uint32_t kind = kHeap;
        if (size >= kBig && MEMAllocFromMappedMemoryEx) {
            raw  = MEMAllocFromMappedMemoryEx(size + kHeader, 0x40);
            kind = kMapped;
        }
        if (!raw && size >= kBig && sAppHeap > 0 && AppHeap()) {
            raw  = MEMAllocFromExpHeapEx(AppHeap(), size + kHeader, -0x40); // negative: from the heap's end
            kind = kApp;
        }
        if (!raw) {
            raw  = std::malloc(size + kHeader);
            kind = kHeap;
        }
        if (!raw) {
            sLastFailed = size;
            return nullptr;
        }
        *static_cast<std::uint32_t *>(raw) = kind;
        return static_cast<char *>(raw) + kHeader;
    }

    void Free(void *p) noexcept {
        if (!p) {
            return;
        }
        char *raw                = static_cast<char *>(p) - kHeader;
        const std::uint32_t kind = *reinterpret_cast<std::uint32_t *>(raw);
        if (kind == kMapped) {
            MEMFreeToMappedMemory(raw);
        } else if (kind == kApp) {
            MEMFreeToExpHeap(AppHeap(), raw);
        } else {
            std::free(raw);
        }
    }
} // namespace

AppHeapFallback::AppHeapFallback() {
    sAppHeap++;
}

AppHeapFallback::~AppHeapFallback() {
    sAppHeap--;
}

std::size_t TakeLastFailedAllocation() {
    const std::size_t s = sLastFailed;
    sLastFailed         = 0;
    return s;
}

std::size_t AppHeapLargestFree() {
    return AppHeap() ? MEMGetAllocatableSizeForExpHeapEx(AppHeap(), 4) : 0;
}

void *operator new(std::size_t n) {
    if (void *p = Alloc(n)) {
        return p;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t n) {
    if (void *p = Alloc(n)) {
        return p;
    }
    throw std::bad_alloc();
}

void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
    return Alloc(n);
}

void *operator new[](std::size_t n, const std::nothrow_t &) noexcept {
    return Alloc(n);
}

void operator delete(void *p) noexcept {
    Free(p);
}

void operator delete[](void *p) noexcept {
    Free(p);
}

void operator delete(void *p, std::size_t) noexcept {
    Free(p);
}

void operator delete[](void *p, std::size_t) noexcept {
    Free(p);
}

void operator delete(void *p, const std::nothrow_t &) noexcept {
    Free(p);
}

void operator delete[](void *p, const std::nothrow_t &) noexcept {
    Free(p);
}
