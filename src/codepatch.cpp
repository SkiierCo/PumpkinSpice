#include "codepatch.h"
#include "menu_builds.h"
#include "utils/logger.h"
#include <coreinit/cache.h>
#include <coreinit/memorymap.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <kernel/kernel.h>
#include <sstream>
#include <string>

// Read by the caves in menu_caves.s (offsets are fixed there).
struct CaveData {
    uint32_t show, is_shown, set_visible, set_black, set_white, find_picture, centre_global;
    float x_off, z_off;
};
static_assert(offsetof(CaveData, centre_global) == 0x18 && offsetof(CaveData, x_off) == 0x1C && offsetof(CaveData, z_off) == 0x20);

extern "C" {
CaveData gCave;
void cave_plaza1();
void cave_plaza2();
void cave_centre();
void cave_page1();
void cave_folder();
}

namespace {

    struct Options {
        bool page1       = false;
        bool folder      = false;
        bool plazaOffset = false, ringRadius = false, crowdRadius = false;
        float x = 0, z = 0;
        float ringX = 0, ringZ = 0;
        float crowdOuter = 0, crowdOuter2 = 0, crowdInner = 0;
        bool any() const { return page1 || folder || plazaOffset || ringRadius || crowdRadius; }
    };

    bool ReadOptions(const std::string &path, Options &o) {
        FILE *f = fopen(path.c_str(), "r");
        if (!f) {
            return false;
        }
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (char *hash = strchr(line, '#')) {
                *hash = '\0';
            }
            std::istringstream in(line);
            std::string key;
            if (!(in >> key)) {
                continue;
            }
            if (key == "page1_hide_right_arrow") {
                o.page1 = true;
            } else if (key == "folder_footer_color") {
                o.folder = true;
            } else if (key == "plaza_offset") {
                o.plazaOffset = static_cast<bool>(in >> o.x >> o.z);
            } else if (key == "plaza_ring_radius") {
                o.ringRadius = static_cast<bool>(in >> o.ringX >> o.ringZ);
            } else if (key == "plaza_crowd_radius") {
                o.crowdRadius = static_cast<bool>(in >> o.crowdOuter >> o.crowdOuter2 >> o.crowdInner);
            } else {
                DEBUG_FUNCTION_LINE_WARN("code.txt: unknown line \"%s\"", key.c_str());
            }
        }
        fclose(f);
        return true;
    }

    uint32_t Read(uint32_t addr) {
        return *reinterpret_cast<volatile uint32_t *>(addr);
    }

    bool Matches(const MenuBuild &b) {
        for (const MenuBlock *blk : {&b.page1, &b.plaza1, &b.plaza2, &b.centre, &b.folder}) {
            for (int i = 0; i < 4; i++) {
                if (Read(blk->addr + 4 * i) != blk->expect[i]) {
                    return false;
                }
            }
        }
        for (const MenuValue *v : {&b.ring_rx, &b.ring_rz, &b.crowd_outer, &b.crowd_outer2, &b.crowd_inner}) {
            if (Read(v->addr) != v->expect) {
                return false;
            }
        }
        return true;
    }

    // Writes one word of Menu memory (code or read-only data) through the kernel, then reads it back.
    bool WriteWord(uint32_t addr, uint32_t value) {
        alignas(0x20) volatile uint32_t buf[8];
        buf[0] = value;
        DCFlushRange((void *) buf, sizeof(buf));
        const uint32_t dst = OSEffectiveToPhysical(addr);
        const uint32_t src = OSEffectiveToPhysical(reinterpret_cast<uint32_t>(&buf[0]));
        if (dst == 0 || src == 0) {
            DEBUG_FUNCTION_LINE_ERR("No physical address for %08X or the buffer", addr);
            return false;
        }
        KernelCopyData(dst, src, 4);
        DCInvalidateRange(reinterpret_cast<void *>(addr), 4);
        ICInvalidateRange(reinterpret_cast<void *>(addr), 4);
        return Read(addr) == value;
    }

    // Replaces the 4-word block with lis r12,cave@h / ori r12,r12,cave@l / mtlr r12 / blrl.
    bool Detour(const MenuBlock &blk, void (*cave)()) {
        const uint32_t target   = reinterpret_cast<uint32_t>(cave);
        const uint32_t words[4] = {0x3D800000 | (target >> 16), 0x618C0000 | (target & 0xFFFF), 0x7D8803A6, 0x4E800021};
        // the call words go last, so the block never jumps into a half-written detour
        for (int i = 3; i >= 0; i--) {
            if (!WriteWord(blk.addr + 4 * i, words[i])) {
                DEBUG_FUNCTION_LINE_ERR("Failed to write the detour at %08X", blk.addr + 4 * i);
                return false;
            }
        }
        return true;
    }

    // The patch has already been applied (or attempted) by the time this runs; only the logging depends on the build.
    void Report(const char *name, bool ok) {
        if (ok) {
            DEBUG_FUNCTION_LINE("%s: applied", name);
        } else {
            DEBUG_FUNCTION_LINE_ERR("%s: failed", name);
        }
    }

    uint32_t FloatBits(float f) {
        uint32_t u;
        memcpy(&u, &f, 4);
        return u;
    }

} // namespace

void ApplyCodePatches(const std::string &themePath) {
    Options o;
    if (!ReadOptions(themePath + "/code.txt", o) || !o.any()) {
        return;
    }
    const MenuBuild *build = nullptr;
    for (const auto &b : kMenuBuilds) {
        if (Matches(b)) {
            build = &b;
            break;
        }
    }
    if (!build) {
        DEBUG_FUNCTION_LINE_ERR("Code patches skipped: this Wii U Menu build is not supported (or its code is already modified)");
        return;
    }
    DEBUG_FUNCTION_LINE("Wii U Menu build: %s", build->name);

    gCave = {build->show, build->is_shown, build->set_visible, build->set_black, build->set_white, build->find_picture,
             build->centre_global, o.x, o.z};
    DCFlushRange(&gCave, sizeof(gCave));

    if (o.plazaOffset) {
        Report("plaza_offset", Detour(build->plaza1, cave_plaza1) && Detour(build->plaza2, cave_plaza2) && Detour(build->centre, cave_centre));
    }
    if (o.page1) {
        Report("page1_hide_right_arrow", Detour(build->page1, cave_page1));
    }
    if (o.folder) {
        Report("folder_footer_color", Detour(build->folder, cave_folder));
    }
    if (o.ringRadius) {
        Report("plaza_ring_radius", WriteWord(build->ring_rx.addr, FloatBits(o.ringX)) && WriteWord(build->ring_rz.addr, FloatBits(o.ringZ)));
    }
    if (o.crowdRadius) {
        Report("plaza_crowd_radius", WriteWord(build->crowd_outer.addr, FloatBits(o.crowdOuter)) &&
                                             WriteWord(build->crowd_outer2.addr, FloatBits(o.crowdOuter2)) &&
                                             WriteWord(build->crowd_inner.addr, FloatBits(o.crowdInner)));
    }
}
