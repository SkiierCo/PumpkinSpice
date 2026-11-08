#include "menu_scan.h"
#include <cstring>

namespace {
    constexpr uint32_t CHUNK     = 0x1000; // text read per step
    constexpr uint32_t MAX_WORDS = 256;    // longest pattern

    uint8_t sWindow[CHUNK + 4 * MAX_WORDS];

    uint32_t Load32(const uint8_t *p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    bool MatchesAt(const uint8_t *p, const MenuPattern &pat) {
        for (uint32_t i = 0; i < pat.count; i++) {
            if ((Load32(p + 4 * i) & pat.words[i][1]) != pat.words[i][0]) {
                return false;
            }
        }
        return true;
    }

    // checks a pattern at a given address (not searched)
    bool MatchesAtAddr(MenuReadFn read, uint32_t addr, const MenuPattern &pat) {
        uint8_t buf[4 * MAX_WORDS];
        return pat.count <= MAX_WORDS && read(addr, buf, 4 * pat.count) && MatchesAt(buf, pat);
    }

    bool ReadWord(MenuReadFn read, uint32_t addr, uint32_t &w) {
        uint8_t b[4];
        if (!read(addr, b, 4)) {
            return false;
        }
        w = Load32(b);
        return true;
    }
} // namespace

bool FindMenuSites(MenuReadFn read, uint32_t textAddr, uint32_t textSize, MenuSites &out, MenuLogFn log) {
    memset(&out, 0, sizeof(out));
    uint32_t hits[MP_COUNT] = {};
    for (const auto &p : kMenuPatterns) {
        if (p.count > MAX_WORDS) {
            log("pattern %s is longer than %u words", p.name, MAX_WORDS);
            return false;
        }
    }

    // one pass over the text: each position is tested as the start of every scanned pattern (key word first)
    uint32_t winAddr = textAddr; // address of sWindow[0]
    uint32_t have    = 0;        // bytes in sWindow
    for (uint32_t off = 0; off < textSize;) {
        const uint32_t n = (textSize - off < CHUNK) ? textSize - off : CHUNK;
        if (!read(textAddr + off, sWindow + have, n)) {
            log("could not read the Menu text at %08X", textAddr + off);
            return false;
        }
        have += n;
        off += n;
        const bool last = off >= textSize;
        // starts tested now: all if this is the end, else those whose longest pattern fits in the window
        const uint32_t limit = last ? have : (have > 4 * MAX_WORDS ? have - 4 * MAX_WORDS + 4 : 0);
        for (uint32_t s = 0; s + 4 <= limit; s += 4) {
            for (int i = 0; i < MP_COUNT; i++) {
                const MenuPattern &p = kMenuPatterns[i];
                if (!p.scan || s + 4 * p.count > have) {
                    continue;
                }
                const uint32_t k = p.key;
                if ((Load32(sWindow + s + 4 * k) & p.words[k][1]) != p.words[k][0] || !MatchesAt(sWindow + s, p)) {
                    continue;
                }
                if (hits[i]++ == 0) {
                    out.site[i] = winAddr + s + 4 * p.site;
                }
            }
        }
        // keep the bytes not yet tested as starts
        memmove(sWindow, sWindow + limit, have - limit);
        winAddr += limit;
        have -= limit;
    }
    for (int i = 0; i < MP_COUNT; i++) {
        const MenuPattern &p = kMenuPatterns[i];
        if (!p.scan) {
            continue;
        }
        if (hits[i] != 1) {
            out.site[i] = 0;
        }
        log("  %-12s %s", p.name, hits[i] == 1 ? "found" : (hits[i] == 0 ? "not found" : "found more than once: not used"));
        if (hits[i] == 1) {
            log("    at %08X", out.site[i]);
        }
    }

    // functions: the target of a call near a found site, or a fixed distance from another function
    for (int i = 0; i < MC_COUNT; i++) {
        const MenuCall &c = kMenuCalls[i];
        uint32_t target   = 0;
        if (c.fixed) {
            if (out.call[c.anchor]) {
                target = out.call[c.anchor] + 4 * c.offset;
            }
        } else if (out.site[c.anchor]) {
            const uint32_t pc = out.site[c.anchor] + 4 * c.offset;
            uint32_t w        = 0;
            if (ReadWord(read, pc, w) && (w >> 26) == 18 && (w & 3) == 1) { // bl
                int32_t d = int32_t(w & 0x03FFFFFC);
                if (d & 0x02000000) {
                    d -= 0x04000000;
                }
                target = pc + d;
            }
        }
        if (target && MatchesAtAddr(read, target, kMenuPatterns[c.verify])) {
            out.call[i] = target;
        }
        log("  %-12s %s", c.name, out.call[i] ? "found" : "not found");
        if (out.call[i]) {
            log("    at %08X", out.call[i]);
        }
    }

    // data: lis rX,addr@ha / load ..,addr@l(rX) in found code
    for (int i = 0; i < MD_COUNT; i++) {
        const MenuData &d = kMenuData[i];
        uint32_t a        = 0;
        uint32_t ha = 0, lo = 0;
        const uint32_t base = out.site[d.anchor];
        if (base && ReadWord(read, base + 4 * d.ha, ha) && ReadWord(read, base + 4 * d.lo, lo) && (ha >> 26) == 15 &&
            ((ha >> 16) & 31) == 0 && ((ha >> 21) & 31) == ((lo >> 16) & 31)) {
            a = (ha << 16) + uint32_t(int32_t(int16_t(lo & 0xFFFF)));
        }
        if (a && d.check) {
            uint32_t v = 0;
            if (!ReadWord(read, a, v) || v != d.stock) {
                log("  %-12s at %08X is %08X, not the stock %08X: not used", d.name, a, v, d.stock);
                a = 0;
            }
        }
        out.data[i] = a;
        log("  %-12s %s", d.name, a ? "found" : "not found");
        if (a) {
            log("    at %08X", a);
        }
    }
    return true;
}

namespace {
    struct Need {
        char kind; // 's' site, 'c' call, 'd' data
        int id;
    };
    struct FeatureNeeds {
        const char *name;
        Need needs[6];
        int count;
    };
    const FeatureNeeds kFeatures[MF_COUNT] = {
            {"plaza_offset", {{'s', MP_PLAZA1}, {'s', MP_PLAZA2}, {'s', MP_CENTRE}, {'d', MD_CENTRE_GLOBAL}}, 4},
            {"page1_hide_right_arrow", {{'s', MP_PAGE1}, {'c', MC_SHOW}, {'c', MC_IS_SHOWN}, {'s', MP_SET_VISIBLE}}, 4},
            {"folder_footer_color", {{'s', MP_FOLDER}, {'c', MC_SET_BLACK}, {'s', MP_SET_WHITE}, {'c', MC_FIND_PICTURE}}, 4},
            {"shrink_long_titles",
             {{'s', MP_TITLE_MIN}, {'s', MP_TITLE_FIT}, {'c', MC_TITLE_RESIZE_MIN}, {'c', MC_TITLE_RESIZE_FIT}, {'s', MP_DRAW_RECT}, {'s', MP_SET_SCALE}},
             6},
            {"plaza_ring_radius", {{'d', MD_RING_RX}, {'d', MD_RING_RZ}}, 2},
            {"plaza_crowd_radius", {{'d', MD_CROWD_OUTER}, {'d', MD_CROWD_OUTER2}, {'d', MD_CROWD_INNER}}, 3},
    };
} // namespace

const char *MenuFeatureName(MenuFeature f) {
    return kFeatures[f].name;
}

bool MenuFeatureReady(const MenuSites &s, MenuFeature f, const char **missing) {
    for (int i = 0; i < kFeatures[f].count; i++) {
        const Need &n    = kFeatures[f].needs[i];
        uint32_t v       = 0;
        const char *name = "";
        if (n.kind == 's') {
            v    = s.site[n.id];
            name = kMenuPatterns[n.id].name;
        } else if (n.kind == 'c') {
            v    = s.call[n.id];
            name = kMenuCalls[n.id].name;
        } else {
            v    = s.data[n.id];
            name = kMenuData[n.id].name;
        }
        if (v == 0) {
            if (missing) {
                *missing = name;
            }
            return false;
        }
    }
    return true;
}
