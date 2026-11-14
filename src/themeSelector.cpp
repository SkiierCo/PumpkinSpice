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
    void StoreString(const char *key, std::string &global, const std::string &value) {
        if (global == value) {
            return;
        }
        global = value;
        if (WUPSStorageAPI::Store(key, global) == WUPS_STORAGE_ERROR_SUCCESS) {
            if (WUPSStorageAPI::SaveStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
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

std::string ShuffleTheme(const std::vector<std::string> &themes) {
    if (themes.empty()) {
        return "";
    }
    std::vector<std::string> pool;
    for (const auto &theme : themes) {
        if (themes.size() < 2 || theme != gLastShuffledTheme) {
            pool.push_back(theme);
        }
    }
    std::mt19937 rng(static_cast<uint32_t>(OSGetTime()));
    const std::string theme = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];
    SDLOG("shuffle: \"%s\" (last time \"%s\")", theme.c_str(), gLastShuffledTheme.c_str());
    return theme;
}

void RememberShuffledTheme(const std::string &theme) {
    StoreString(LAST_SHUFFLED_THEME_STRING, gLastShuffledTheme, theme);
}
