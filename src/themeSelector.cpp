// SDCafiine's HandleMultiModPacks (modpackSelector.cpp), for themes instead of a game's modpacks. SDCafiine's
// autostart was for a single modpack only; here MINUS sets autostart for the selected theme, with any number of themes.
#include "themeSelector.h"
#include "bootscreen.h"
#include "globals.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include <algorithm>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <random>
#include <vpad/input.h>
#include <wups/storage.h>

#define TEXT_SEL(x, text1, text2) ((x) ? (text1) : (text2))

namespace {
    void StoreString(const char *key, std::string &global, const std::string &value, bool save = true) {
        if (global == value) {
            return;
        }
        global = value;
        if (WUPSStorageAPI::Store(key, global) == WUPS_STORAGE_ERROR_SUCCESS) {
            if (save && WUPSStorageAPI::SaveStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
                DEBUG_FUNCTION_LINE_ERR("Failed to close storage");
            }
        } else {
            DEBUG_FUNCTION_LINE_WARN("Failed to save to storage");
        }
    }
} // namespace

std::string SelectTheme(const std::vector<std::string> &themes) {
    if (themes.empty()) {
        return "";
    }
    InputInit();
    const bool minusHeld = ReadButtonsHeld() & VPAD_BUTTON_MINUS;
    // MINUS held while the Menu starts: the list is shown, and autostart is cleared (shuffle stays on)
    if (minusHeld && !gShuffleThemes && !gAutostartTheme.empty()) {
        SDLOG("theme selector: MINUS held, autostart (\"%s\") cleared", gAutostartTheme.c_str());
        StoreString(AUTOSTART_THEME_STRING, gAutostartTheme, "");
    }
    // the theme started without the list: shuffle's pick, else the autostart theme ("" = the list is shown)
    std::string startTheme;
    if (gShuffleThemes) {
        if (!minusHeld) {
            startTheme = ShuffleTheme(themes);
        }
    } else if (std::find(themes.begin(), themes.end(), gAutostartTheme) != themes.end()) {
        startTheme = gAutostartTheme;
    }
    const bool autostart = !startTheme.empty();
    if (autostart && gSkipPrepareScreen) {
        InputDeInit();
        return startTheme;
    }

    // the autostart (or shuffled) theme, else the theme selected in the config menu, is selected here at first
    int selected = 0;
    if (auto it = std::find(themes.begin(), themes.end(), autostart ? startTheme : gActiveTheme); it != themes.end()) {
        selected = int(it - themes.begin());
    }
    int initScreen = 1;
    int x_offset   = -2;

    int page      = 0;
    int per_page  = 11;
    int max_pages = (themes.size() / per_page) + (themes.size() % per_page != 0 ? 1 : 0);

    int curState = autostart ? 1 : 0;

    // "Preparing theme" for the autostart theme: 1 second to press MINUS (SDCafiine: 60 frames)
    const OSTime prepareStart = OSGetTime();
    std::string result;

    // MINUS only counts again once it's been let go. Without this a MINUS held at startup (or held a bit too long on
    // "Preparing theme...") came through as a new press in the list and turned autostart right back on for the
    // selected theme, so A then started it with autostart on again
    bool minusLocked = minusHeld;

    if (!ScreenInit()) {
        InputDeInit();
        SDLOG("theme selector: no screen, using \"%s\"", themes[selected].c_str());
        return themes[selected];
    }

    while (true) {
        uint32_t buttonsHeld      = 0;
        uint32_t buttonsTriggered = ReadButtonsTriggered(&buttonsHeld);
        if (minusLocked) {
            if (!(buttonsHeld & VPAD_BUTTON_MINUS)) {
                minusLocked = false;
            }
            buttonsTriggered &= ~VPAD_BUTTON_MINUS;
        }

        if (curState == 1) {
            if (buttonsTriggered & VPAD_BUTTON_MINUS) {
                curState    = 0;
                initScreen  = 1;
                minusLocked = true;
                continue;
            }

            if (initScreen) {
                ScreenClear();
                ScreenPrint(x_offset, -1, "PumpkinSpice " VERSION_FULL);
                ScreenPrint(x_offset, 1, "Preparing theme \"%s\"...", startTheme.c_str());
                ScreenPrint(x_offset, 3, "Press MINUS to open menu");
                ScreenFlip();
                initScreen = 0;
            }

            if (OSTicksToMilliseconds(OSGetTime() - prepareStart) >= 1000) {
                result = startTheme;
                break;
            }
        } else {
            bool wantToExit = false;
            if (buttonsTriggered & VPAD_BUTTON_A) {
                wantToExit = true;
            } else if (buttonsTriggered & VPAD_BUTTON_MINUS) {
                ScreenClear();
                ScreenPrint(x_offset, -1, "PumpkinSpice " VERSION_FULL);
                ScreenPrint(x_offset, 1, "Save settings...");
                ScreenFlip();

                // the selected theme becomes the autostart theme, or autostart is turned off if it already is
                StoreString(AUTOSTART_THEME_STRING, gAutostartTheme, gAutostartTheme == themes[selected] ? "" : themes[selected]);
                initScreen  = 1;
                minusLocked = true;
            } else if (buttonsTriggered & VPAD_BUTTON_B) {
                SDLOG("theme selector: started without a theme");
                break;
            } else if (buttonsTriggered & VPAD_BUTTON_DOWN) {
                selected++;
                initScreen = 1;
            } else if (buttonsTriggered & VPAD_BUTTON_UP) {
                selected--;
                initScreen = 1;
            } else if (buttonsTriggered & VPAD_BUTTON_L) {
                selected -= per_page;
                initScreen = 1;
            } else if (buttonsTriggered & VPAD_BUTTON_R) {
                selected += per_page;
                initScreen = 1;
            }
            if (selected < 0) {
                selected = 0;
            }
            if (selected >= (int) themes.size()) {
                selected = themes.size() - 1;
            }
            page = selected / per_page;

            if (wantToExit) {
                result = themes[selected];
                StoreString(ACTIVE_THEME_STRING, gActiveTheme, result);
                break;
            }

            if (initScreen) {
                ScreenClear();
                ScreenPrint(x_offset, -1, "PumpkinSpice " VERSION_FULL);
                ScreenPrint(x_offset, 1, "Press A to launch a theme");
                ScreenPrint(x_offset, 2, "Press B to launch without a theme");
                if (gAutostartTheme == themes[selected]) {
                    ScreenPrint(x_offset, 4, "Press MINUS to disable autostart for the selected theme");
                } else {
                    ScreenPrint(x_offset, 4, "Press MINUS to enable autostart for the selected theme");
                }
                int y_offset = 6;
                for (int cur = page * per_page; cur < (int) themes.size() && cur < (page + 1) * per_page; cur++) {
                    ScreenPrint(x_offset, y_offset++, "%s %s%s", TEXT_SEL((selected == cur), "--->", "    "), themes[cur].c_str(),
                                TEXT_SEL((gAutostartTheme == themes[cur]), " (autostart)", ""));
                }

                if (max_pages > 0) {
                    ScreenPrint(x_offset, 17, "Page %02d/%02d. Press L/R to change page.", page + 1, max_pages);
                }
                ScreenFlip();
                initScreen = 0;
            }
        }
        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    ScreenDeInit();
    InputDeInit();
    if (!result.empty()) {
        SDLOG("theme selector: \"%s\"", result.c_str());
    }
    return result;
}

namespace {
    constexpr size_t kNoRepeat   = 5;  // a theme can't come back until 5 other themes were shuffled in
    constexpr size_t kHistoryMax = 32; // how many shuffled themes are remembered

    // the history string split up, newest first
    std::vector<std::string> SplitHistory(const std::string &history) {
        std::vector<std::string> out;
        size_t start = 0;
        while (start < history.size()) {
            size_t end = history.find('|', start);
            if (end == std::string::npos) {
                end = history.size();
            }
            if (end > start) {
                out.push_back(history.substr(start, end - start));
            }
            start = end + 1;
        }
        return out;
    }

    // one generator for the whole boot, seeded from the clock the first time it's used, so every shuffle after that
    // keeps going from where the last one left off instead of starting over from a new clock value
    std::mt19937 &Rng() {
        static std::mt19937 rng([] {
            const uint64_t t = static_cast<uint64_t>(OSGetTime());
            return static_cast<uint32_t>(t ^ (t >> 32));
        }());
        return rng;
    }
} // namespace

// How it picks: every item gets a weight from how long ago shuffle last used it. The last 5 different items get 0 so
// none of them can come back yet (with 5 items or fewer it's all of them but one). After that the weight goes up by one
// for every item used since, so older items are more likely but nothing is ever guaranteed. An item that isn't in the
// history at all (new or not used in a long time) counts as the oldest.
std::string ShufflePick(const std::vector<std::string> &items, const std::string &history, const char *what) {
    if (items.empty()) {
        return "";
    }
    // the history's items that are still there, each once (its newest spot), newest first
    std::vector<std::string> recent;
    for (const auto &name : SplitHistory(history)) {
        if (std::find(items.begin(), items.end(), name) != items.end() &&
            std::find(recent.begin(), recent.end(), name) == recent.end()) {
            recent.push_back(name);
        }
    }
    const size_t blocked = std::min(kNoRepeat, items.size() - 1);
    // not in the history: older than everything in it, and never blocked even while the history is still short
    const size_t unseenAge = std::max(recent.size(), blocked) + 1;
    std::vector<double> weights;
    for (const auto &item : items) {
        const auto it    = std::find(recent.begin(), recent.end(), item);
        const size_t age = it == recent.end() ? unseenAge : size_t(it - recent.begin()) + 1; // 1 = last time
        weights.push_back(age <= blocked ? 0.0 : double(age - blocked));
    }
    std::discrete_distribution<size_t> pick(weights.begin(), weights.end());
    const std::string chosen = items[pick(Rng())];
#ifdef PS_SDLOG
    std::string odds;
    double total = 0;
    for (double w : weights) {
        total += w;
    }
    for (size_t i = 0; i < items.size(); i++) {
        odds += " \"" + items[i] + "\" " + std::to_string(int(weights[i] * 100.0 / total + 0.5)) + "%";
    }
    SDLOG("shuffle %s: \"%s\" (odds:%s)", what, chosen.c_str(), odds.c_str());
#else
    (void) what;
#endif
    return chosen;
}

void RememberShuffled(const char *key, std::string &history, const std::string &item, bool save) {
    std::string updated = item;
    size_t kept         = 1;
    for (const auto &name : SplitHistory(history)) {
        if (kept++ >= kHistoryMax) {
            break;
        }
        updated += "|" + name;
    }
    StoreString(key, history, updated, save);
}

uint32_t ShuffleRandom(uint32_t n) {
    return n ? std::uniform_int_distribution<uint32_t>(0, n - 1)(Rng()) : 0;
}

std::string ShuffleTheme(const std::vector<std::string> &themes) {
    return ShufflePick(themes, gShuffleHistory, "theme");
}

void RememberShuffledTheme(const std::string &theme) {
    RememberShuffled(SHUFFLE_HISTORY_STRING, gShuffleHistory, theme);
}
