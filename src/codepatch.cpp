#include "codepatch.h"
#include "menu_builds.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include <coreinit/cache.h>
#include <coreinit/dynload.h>
#include <coreinit/memorymap.h>
#include <coreinit/thread.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <kernel/kernel.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Read by the caves in menu_caves.s (offsets are fixed there).
struct CaveData {
    uint32_t show, is_shown, set_visible, set_black, set_white, find_picture, centre_global;
    float x_off, z_off;
    uint32_t title_resize_min, title_resize_fit, draw_rect, set_scale;
};
static_assert(offsetof(CaveData, centre_global) == 0x18 && offsetof(CaveData, x_off) == 0x1C && offsetof(CaveData, z_off) == 0x20);
static_assert(offsetof(CaveData, title_resize_min) == 0x24 && offsetof(CaveData, set_scale) == 0x30);

extern "C" {
CaveData gCave;
void cave_plaza1();
void cave_plaza2();
void cave_centre();
void cave_page1();
void cave_folder();
void cave_title_min();
void cave_title_fit();
}

namespace {

    struct Options {
        bool page1       = false;
        bool folder      = false;
        bool shrinkTitle = false;
        bool plazaOffset = false, ringRadius = false, crowdRadius = false;
        float x = 0, z = 0;
        float ringX = 0, ringZ = 0;
        float crowdOuter = 0, crowdOuter2 = 0, crowdInner = 0;
        bool any() const { return page1 || folder || shrinkTitle || plazaOffset || ringRadius || crowdRadius; }
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
            } else if (key == "shrink_long_titles") {
                o.shrinkTitle = true;
            } else {
                DEBUG_FUNCTION_LINE_WARN("code.txt: unknown line \"%s\"", key.c_str());
            }
        }
        fclose(f);
        return true;
    }

    // Physical address of plugin/Menu memory. Aroma's module and plugin memory (0x00800000 - 0x01000000) isn't
    // covered by OSEffectiveToPhysical; it sits at a fixed offset (same mapping as FunctionPatcherModule uses).
    uint32_t PhysOf(uint32_t addr) {
        if (addr >= 0x00800000 && addr < 0x01000000) {
            return addr + 0x30800000 - 0x00800000;
        }
        uint32_t phys = OSEffectiveToPhysical(addr);
        if (phys == 0 && addr >= 0x01000000 && addr < 0x10000000) {
            // the running app's code area (homebrew_on_menu_plugin uses the same mapping for Menu code)
            phys = addr + 0x31000000;
        }
        return phys;
    }

    // Reads one word of Menu (or coreinit) memory through the kernel, by physical address, like FunctionPatcherModule.
    uint32_t Read(uint32_t addr) {
        alignas(0x20) volatile uint32_t buf[8] = {};
        const uint32_t src                     = PhysOf(addr);
        const uint32_t dst                     = PhysOf(reinterpret_cast<uint32_t>(&buf[0]));
        if (src == 0 || dst == 0) {
            SDLOG("  no physical address for %08X (%08X) or the buffer (%08X)", addr, src, dst);
            return 0xFFFFFFFF;
        }
        DCFlushRange((void *) buf, sizeof(buf));
        KernelCopyData(dst, src, 4);
        DCFlushRange((void *) buf, sizeof(buf));
        return buf[0];
    }

    bool WriteWord(uint32_t addr, uint32_t value);

    // Where the loader put men.rpx: the build table holds link addresses (where Cemu runs it); on the console text and
    // data are moved. textDelta/dataDelta are added to link addresses (text links at 0x02000000, data at 0x10000000).
    struct MenuPlacement {
        bool found         = false;
        uint32_t textDelta = 0;
        uint32_t dataDelta = 0;
    };

    // OSDynLoad_GetNumberOfRPLs/GetRPLInfo return nothing on retail CafeOS because of one branch each; like RosePatcher,
    // that branch becomes a nop (left as it is if another plugin already did it), then men.rpx is looked up.
    // The real address of a coreinit function (taking the address of an import gives the plugin's own stub).
    uint32_t CoreinitFunction(const char *name) {
        OSDynLoad_Module coreinit = nullptr;
        void *addr                = nullptr;
        if (OSDynLoad_Acquire("coreinit.rpl", &coreinit) != OS_DYNLOAD_OK) {
            return 0;
        }
        if (OSDynLoad_FindExport(coreinit, OS_DYNLOAD_EXPORT_FUNC, name, &addr) != OS_DYNLOAD_OK) {
            addr = nullptr;
        }
        OSDynLoad_Release(coreinit);
        return reinterpret_cast<uint32_t>(addr);
    }

    MenuPlacement FindMenuRpl() {
        MenuPlacement p;
        const uint32_t getNumber = CoreinitFunction("OSDynLoad_GetNumberOfRPLs");
        const uint32_t getInfo   = CoreinitFunction("OSDynLoad_GetRPLInfo");
        SDLOG("  coreinit OSDynLoad_GetNumberOfRPLs %08X, OSDynLoad_GetRPLInfo %08X", getNumber, getInfo);
        if (getNumber == 0 || getInfo == 0) {
            return p;
        }
        struct Site {
            uint32_t addr, original;
            bool patched;
        } sites[] = {{getNumber + 6 * 4, 0x41820038, false}, {getInfo + 22 * 4, 0x41820100, false}};
        for (auto &s : sites) {
            const uint32_t now = Read(s.addr);
            SDLOG("  dynload check at %08X: %08X", s.addr, now);
            if (now == s.original) {
                s.patched = WriteWord(s.addr, 0x60000000);
                ICInvalidateRange(reinterpret_cast<void *>(s.addr), 4);
            } else if (now != 0x60000000) {
                SDLOG("  unexpected instruction: RPL info not available");
                return p;
            }
        }
        const int count = OSDynLoad_GetNumberOfRPLs();
        SDLOG("  %d RPLs loaded", count);
        if (count > 0) {
            std::vector<OSDynLoad_NotifyData> rpls(count);
            if (OSDynLoad_GetRPLInfo(0, count, rpls.data())) {
                for (const auto &r : rpls) {
                    if (r.name == nullptr) {
                        continue;
                    }
                    const std::string_view name(r.name);
                    if (name.size() >= 7 && name.substr(name.size() - 7) == "men.rpx") {
                        SDLOG("  %s: text %08X size %08X, data %08X size %08X, read %08X", r.name, r.textAddr, r.textSize,
                              r.dataAddr, r.dataSize, r.readAddr);
                        p.found     = true;
                        p.textDelta = r.textAddr - 0x02000000;
                        p.dataDelta = r.dataAddr - 0x10000000;
                        break;
                    }
                }
            }
        }
        for (auto &s : sites) { // put coreinit back the way it was
            if (s.patched) {
                WriteWord(s.addr, s.original);
                ICInvalidateRange(reinterpret_cast<void *>(s.addr), 4);
            }
        }
        return p;
    }

    MenuBuild OnConsole(const MenuBuild &b, const MenuPlacement &p) {
        MenuBuild r = b;
        for (MenuBlock *blk : {&r.page1, &r.plaza1, &r.plaza2, &r.centre, &r.folder}) {
            blk->addr += p.textDelta;
        }
        for (MenuBlock *blk : {&r.title_min, &r.title_fit}) {
            blk->addr += p.textDelta;
        }
        for (uint32_t *fn : {&r.show, &r.is_shown, &r.set_visible, &r.set_black, &r.set_white, &r.find_picture,
                             &r.title_resize_min, &r.title_resize_fit, &r.draw_rect, &r.set_scale}) {
            *fn += p.textDelta;
        }
        r.centre_global += p.dataDelta;
        // the centre block's 3rd word is "lwzu r0,G@l(r24)": the loader writes the moved G's low half into it
        r.centre.expect[2] = (r.centre.expect[2] & 0xFFFF0000) | (r.centre_global & 0xFFFF);
        for (MenuValue *v : {&r.ring_rx, &r.ring_rz, &r.crowd_outer, &r.crowd_outer2, &r.crowd_inner}) {
            v->addr += p.dataDelta;
        }
        return r;
    }

    // One logged kernel copy of a marker word from `from` to `to` (both effective addresses, own cache lines).
    bool TestCopy(const char *what, volatile uint32_t *from, volatile uint32_t *to) {
        const uint32_t src = PhysOf(reinterpret_cast<uint32_t>(from));
        const uint32_t dst = PhysOf(reinterpret_cast<uint32_t>(to));
        *from              = 0x50534B43; // "PSKC"
        *to                = 0;
        DCFlushRange((void *) from, 4);
        DCFlushRange((void *) to, 4);
        SDLOG("copy test %s: %08X (phys %08X) -> %08X (phys %08X)", what, reinterpret_cast<uint32_t>(from), src,
              reinterpret_cast<uint32_t>(to), dst);
        if (src == 0 || dst == 0) {
            SDLOG("  no physical address");
            return false;
        }
        KernelCopyData(dst, src, 4);
        DCFlushRange((void *) to, 4);
        SDLOG("  returned, result %08X (expected 50534B43)", *to);
        return *to == 0x50534B43;
    }

    // Checks that a kernel copy works before any Menu code is written (stack to stack, as the writes use the stack).
    bool KernelCopySelfTest() {
        alignas(0x20) volatile uint32_t a[8] = {};
        alignas(0x20) volatile uint32_t b[8] = {};
        return TestCopy("stack -> stack", &a[0], &b[0]);
    }

    // Patched code ranges; their instruction cache lines are invalidated on every core once all patches are written.
    struct CodeRange {
        uint32_t addr, size;
    };
    CodeRange sCodeRanges[8];
    int sCodeRangeCount = 0;

    int InvalidateCodeRanges(int, const char **) {
        for (int i = 0; i < sCodeRangeCount; i++) {
            ICInvalidateRange(reinterpret_cast<void *>(sCodeRanges[i].addr), sCodeRanges[i].size);
        }
        return 0;
    }

    void InvalidateCodeOnAllCores() {
        alignas(16) static OSThread thread;
        alignas(16) static uint8_t stack[0x2000];
        for (int core = 0; core < 3; core++) {
            if (!OSCreateThread(&thread, InvalidateCodeRanges, 0, nullptr, stack + sizeof(stack), sizeof(stack), 16,
                                static_cast<OSThreadAttributes>(1 << core))) {
                SDLOG("  could not start the cache thread on core %d", core);
                continue;
            }
            OSResumeThread(&thread);
            int result = 0;
            OSJoinThread(&thread, &result);
        }
        SDLOG("instruction cache invalidated on all cores (%d ranges)", sCodeRangeCount);
    }

    bool MatchesCode(const MenuBuild &b) {
        for (const MenuBlock *blk : {&b.page1, &b.plaza1, &b.plaza2, &b.centre, &b.folder, &b.title_min, &b.title_fit}) {
            for (int i = 0; i < 4; i++) {
                if (Read(blk->addr + 4 * i) != blk->expect[i]) {
                    SDLOG("  %s: no match at %08X (%08X, expected %08X)", b.name, blk->addr + 4 * i, Read(blk->addr + 4 * i), blk->expect[i]);
                    return false;
                }
            }
        }
        return true;
    }

    bool MatchesData(const MenuBuild &b) {
        for (const MenuValue *v : {&b.ring_rx, &b.ring_rz, &b.crowd_outer, &b.crowd_outer2, &b.crowd_inner}) {
            if (Read(v->addr) != v->expect) {
                SDLOG("  %s: no match at %08X (%08X, expected %08X)", b.name, v->addr, Read(v->addr), v->expect);
                return false;
            }
        }
        return true;
    }

    // Writes one word of Menu memory (code or read-only data) through the kernel from a stack buffer, flushes the
    // written address (as RosePatcher does), then reads it back through the kernel.
    bool WriteWord(uint32_t addr, uint32_t value) {
        alignas(0x20) volatile uint32_t buf[8] = {};
        buf[0]                                 = value;
        const uint32_t dst                     = PhysOf(addr);
        const uint32_t src                     = PhysOf(reinterpret_cast<uint32_t>(&buf[0]));
        SDLOG("  write %08X (phys %08X) %08X -> %08X, buffer phys %08X", addr, dst, Read(addr), value, src);
        if (dst == 0 || src == 0) {
            DEBUG_FUNCTION_LINE_ERR("No physical address for %08X or the buffer", addr);
            return false;
        }
        DCFlushRange((void *) buf, sizeof(buf));
        KernelCopyData(dst, src, 4);
        SDLOG("    copied");
        DCFlushRange(reinterpret_cast<void *>(addr), 4); // as RosePatcher does after its kernel writes
        const uint32_t back = Read(addr);
        SDLOG("    read back %08X", back);
        return back == value;
    }

    // Replaces the 4-word block with lis r12,cave@h / ori r12,r12,cave@l / mtlr r12 / blrl.
    bool Detour(const MenuBlock &blk, void (*cave)()) {
        const uint32_t target   = reinterpret_cast<uint32_t>(cave);
        const uint32_t words[4] = {0x3D800000 | (target >> 16), 0x618C0000 | (target & 0xFFFF), 0x7D8803A6, 0x4E800021};
        // the call words go last, so the block never jumps into a half-written detour
        if (sCodeRangeCount < static_cast<int>(sizeof(sCodeRanges) / sizeof(sCodeRanges[0]))) {
            sCodeRanges[sCodeRangeCount++] = {blk.addr, 16};
        }
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
        SDLOG("%s: %s", name, ok ? "applied" : "FAILED");
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
        SDLOG("code.txt: none or no active lines");
        return;
    }
    SDLOG("code.txt: page1 %d folder %d shrink_long_titles %d plaza_offset %d (%g %g) ring %d crowd %d", o.page1, o.folder,
          o.shrinkTitle, o.plazaOffset, o.x, o.z, o.ringRadius, o.crowdRadius);
    if (!KernelCopySelfTest()) {
        SDLOG("kernel copy self-test failed: code patches skipped");
        return;
    }
    const MenuPlacement placement = FindMenuRpl();
    if (!placement.found) {
        SDLOG("men.rpx not found in the RPL list: code patches skipped");
        DEBUG_FUNCTION_LINE_ERR("Code patches skipped: men.rpx not found");
        return;
    }
    SDLOG("men.rpx placement: text %+d, data %+d", static_cast<int>(placement.textDelta), static_cast<int>(placement.dataDelta));
    MenuBuild found{};
    const MenuBuild *build = nullptr;
    for (const auto &b : kMenuBuilds) {
        found = OnConsole(b, placement);
        SDLOG("checking build %s (page1 block at %08X, OSEffectiveToPhysical %08X, used %08X)", found.name, found.page1.addr,
              OSEffectiveToPhysical(found.page1.addr), PhysOf(found.page1.addr));
        if (MatchesCode(found)) {
            build = &found;
            break;
        }
    }
    if (!build) {
        SDLOG("no supported build matched: code patches skipped");
        DEBUG_FUNCTION_LINE_ERR("Code patches skipped: this Wii U Menu build is not supported (or its code is already modified)");
        return;
    }
    SDLOG("Wii U Menu build: %s", build->name);
    DEBUG_FUNCTION_LINE("Wii U Menu build: %s", build->name);
    const bool dataOk = MatchesData(*build);
    SDLOG("Plaza data constants %s", dataOk ? "found" : "not found: radius patches skipped");

    gCave = {build->show, build->is_shown, build->set_visible, build->set_black,
             build->set_white, build->find_picture, build->centre_global, o.x,
             o.z, build->title_resize_min, build->title_resize_fit, build->draw_rect,
             build->set_scale};
    DCFlushRange(&gCave, sizeof(gCave));
    SDLOG("gCave %08X; caves page1 %08X folder %08X plaza1 %08X plaza2 %08X centre %08X (phys of page1 cave %08X)",
          reinterpret_cast<uint32_t>(&gCave), reinterpret_cast<uint32_t>(cave_page1), reinterpret_cast<uint32_t>(cave_folder),
          reinterpret_cast<uint32_t>(cave_plaza1), reinterpret_cast<uint32_t>(cave_plaza2), reinterpret_cast<uint32_t>(cave_centre),
          PhysOf(reinterpret_cast<uint32_t>(cave_page1)));
    sCodeRangeCount = 0;

    if (o.plazaOffset) {
        Report("plaza_offset", Detour(build->plaza1, cave_plaza1) && Detour(build->plaza2, cave_plaza2) && Detour(build->centre, cave_centre));
    }
    if (o.page1) {
        Report("page1_hide_right_arrow", Detour(build->page1, cave_page1));
    }
    if (o.folder) {
        Report("folder_footer_color", Detour(build->folder, cave_folder));
    }
    if (o.shrinkTitle) {
        Report("shrink_long_titles", Detour(build->title_min, cave_title_min) && Detour(build->title_fit, cave_title_fit));
    }
    if (o.ringRadius && dataOk) {
        Report("plaza_ring_radius", WriteWord(build->ring_rx.addr, FloatBits(o.ringX)) && WriteWord(build->ring_rz.addr, FloatBits(o.ringZ)));
    }
    if (o.crowdRadius && dataOk) {
        Report("plaza_crowd_radius", WriteWord(build->crowd_outer.addr, FloatBits(o.crowdOuter)) &&
                                             WriteWord(build->crowd_outer2.addr, FloatBits(o.crowdOuter2)) &&
                                             WriteWord(build->crowd_inner.addr, FloatBits(o.crowdInner)));
    }
    if (sCodeRangeCount > 0) {
        InvalidateCodeOnAllCores();
    }
    SDLOG("code patches done");
}
