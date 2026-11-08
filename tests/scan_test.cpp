// Runs src/menu_scan.cpp on Menu images written by make_scan_test.py (Persona Advanced RELOADED project, rpx
// folder) and compares what it finds with the expected addresses.
// build: g++ -std=c++17 -O2 -I../src scan_test.cpp ../src/menu_scan.cpp -o scan_test
// run:   ./scan_test <case dir> [...]
#include "menu_scan.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
    std::vector<uint8_t> gText, gData;
    uint32_t gTextAddr = 0, gDataAddr = 0;
    bool gVerbose      = false;

    bool Read(uint32_t addr, void *dst, uint32_t size) {
        if (addr >= gTextAddr && addr + size <= gTextAddr + gText.size()) {
            memcpy(dst, gText.data() + (addr - gTextAddr), size);
            return true;
        }
        if (addr >= gDataAddr && addr + size <= gDataAddr + gData.size()) {
            memcpy(dst, gData.data() + (addr - gDataAddr), size);
            return true;
        }
        return false;
    }

    void Log(const char *fmt, ...) {
        if (!gVerbose) {
            return;
        }
        va_list va;
        va_start(va, fmt);
        vprintf(fmt, va);
        va_end(va);
        putchar('\n');
    }

    std::vector<uint8_t> Load(const std::string &path) {
        std::ifstream f(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
} // namespace

int main(int argc, char **argv) {
    int failed = 0;
    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-v") == 0) {
            gVerbose = true;
            continue;
        }
        const std::string dir = argv[a];
        std::ifstream c(dir + "/case.txt");
        std::map<std::string, uint32_t> want;
        std::string key, v1, v2;
        uint32_t textSize = 0;
        std::string line;
        while (std::getline(c, line)) {
            std::istringstream in(line);
            in >> key >> v1;
            if (key == "text") {
                in >> v2;
                gTextAddr = std::stoul(v1, nullptr, 16);
                textSize  = std::stoul(v2, nullptr, 16);
            } else if (key == "data") {
                gDataAddr = std::stoul(v1, nullptr, 16);
            } else if (!key.empty()) {
                want[key] = std::stoul(v1, nullptr, 16);
            }
        }
        gText = Load(dir + "/text.bin");
        gData = Load(dir + "/data.bin");
        MenuSites s;
        if (!FindMenuSites(Read, gTextAddr, textSize, s, Log)) {
            printf("FAIL %s: scan could not read the text\n", dir.c_str());
            failed++;
            continue;
        }
        std::map<std::string, uint32_t> got;
        for (int i = 0; i < MP_COUNT; i++) {
            if (kMenuPatterns[i].scan) {
                got[std::string("site.") + kMenuPatterns[i].name] = s.site[i];
            }
        }
        for (int i = 0; i < MC_COUNT; i++) {
            got[std::string("call.") + kMenuCalls[i].name] = s.call[i];
        }
        for (int i = 0; i < MD_COUNT; i++) {
            got[std::string("data.") + kMenuData[i].name] = s.data[i];
        }
        for (int f = 0; f < MF_COUNT; f++) {
            got[std::string("feature.") + MenuFeatureName(MenuFeature(f))] = MenuFeatureReady(s, MenuFeature(f), nullptr) ? 1 : 0;
        }
        int bad = 0;
        for (const auto &[k, v] : want) {
            if (v == 0xFFFFFFFF) {
                continue;
            }
            auto it = got.find(k);
            if (it == got.end()) {
                printf("  %s: %s not reported\n", dir.c_str(), k.c_str());
                bad++;
            } else if (it->second != v) {
                printf("  %s: %s = %08X, want %08X\n", dir.c_str(), k.c_str(), it->second, v);
                bad++;
            }
        }
        if (got.size() != want.size()) {
            printf("  %s: reports %zu values, case has %zu\n", dir.c_str(), got.size(), want.size());
            bad++;
        }
        printf("%s %s\n", bad ? "FAIL" : "ok  ", dir.c_str());
        failed += bad ? 1 : 0;
    }
    printf("%d failed\n", failed);
    return failed ? 1 : 0;
}
