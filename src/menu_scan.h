#pragma once
#include "menu_patterns.h"
#include <cstdint>

// Finds the code patch sites in the running Wii U Menu with the patterns of menu_patterns.h: every scanned pattern
// is searched in the whole men.rpx text and must match exactly once; functions are taken from calls near found
// sites (their own window must match there); data addresses are decoded from lis/load pairs in found code.
// Anything not found stays 0, so each code patch can check for the sites it needs.
// No Wii U calls here (memory goes through `read`), so this also runs in a test on a PC.

// reads `size` bytes of Menu memory at `addr`; false if it can't
typedef bool (*MenuReadFn)(uint32_t addr, void *dst, uint32_t size);
typedef void (*MenuLogFn)(const char *fmt, ...);

struct MenuSites {
    uint32_t site[MP_COUNT]; // address of each pattern's site (scanned patterns; function windows stay 0)
    uint32_t call[MC_COUNT]; // Menu functions
    uint32_t data[MD_COUNT]; // data addresses
};

// false if the text could not be read (then nothing is found)
bool FindMenuSites(MenuReadFn read, uint32_t textAddr, uint32_t textSize, MenuSites &out, MenuLogFn log);

// the code.txt patches and the sites each one needs
enum MenuFeature {
    MF_PLAZA_OFFSET,
    MF_PAGE1,
    MF_FOLDER,
    MF_SHRINK_TITLES,
    MF_RING_RADIUS,
    MF_CROWD_RADIUS,
    MF_COUNT
};

const char *MenuFeatureName(MenuFeature f);

// true if every site the patch needs was found; else `missing` names the first one that wasn't
bool MenuFeatureReady(const MenuSites &s, MenuFeature f, const char **missing);
