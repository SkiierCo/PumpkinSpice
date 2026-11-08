#pragma once
#include <cstddef>

// While one of these exists (a theme package being rebuilt, before the Wii U Menu runs), a block of 256 KiB or more that
// Aroma's mapped memory can't give comes from the Wii U Menu's own heap (MEM2) instead, taken from its far end. Every
// such block is freed before the rebuild ends, so the Menu starts with its heap as it was.
struct AppHeapFallback {
    AppHeapFallback();
    ~AppHeapFallback();
    AppHeapFallback(const AppHeapFallback &) = delete;
    AppHeapFallback &operator=(const AppHeapFallback &) = delete;
};

// For the SD log: the size of the last allocation that failed (0 if none since the last call) and the largest block
// the Menu's heap can give
std::size_t TakeLastFailedAllocation();
std::size_t AppHeapLargestFree();
