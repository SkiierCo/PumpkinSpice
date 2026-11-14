#include "globals.h"
#include "music.h"
#include "package.h"
#include "theme.h"
#include "themeSelector.h"
#include "utils/config.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include <algorithm>
#include <content_redirection/redirection.h>
#include <coreinit/title.h>
#include <wups.h>

WUPS_PLUGIN_NAME("PumpkinSpice");
WUPS_PLUGIN_DESCRIPTION("Loads Wii U Menu themes from the SD card");
WUPS_PLUGIN_VERSION(VERSION_FULL);
WUPS_PLUGIN_AUTHOR("Fil (based on SDCafiine by Maschell)");
WUPS_PLUGIN_LICENSE("GPL");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("pumpkinspice"); // Unique id for the storage api

INITIALIZE_PLUGIN() {
    ContentRedirectionStatus error;
    if ((error = ContentRedirection_InitLibrary()) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init ContentRedirection. Error %s %d", ContentRedirection_GetStatusStr(error), error);
        OSFatal("PumpkinSpice: Failed to init ContentRedirection.");
    }

    InitStorageAndConfig();
    gLayerHandles.clear();
    SDLOG_NEW_BOOT(); // plugins load once per boot
    SDLOG("=== PumpkinSpice %s loaded", VERSION_FULL);
}

ON_APPLICATION_START() {
    initLogging();
    if (!IsWiiUMenu(OSGetTitleID())) {
        return;
    }
    SDLOG("=== Wii U Menu start %016llX, PumpkinSpice %s, enabled %d", OSGetTitleID(), VERSION_FULL, gPumpkinSpiceEnabled);
    if (!gPumpkinSpiceEnabled) {
        DEBUG_FUNCTION_LINE("PumpkinSpice is disabled");
        SwitchOnStyleMiiU(); // if PumpkinSpice switched it off, it loads again from the next boot
    } else if (SwitchOffStyleMiiU()) {
        // renamed now, but loaded since boot: the two would clash this boot, the theme applies from the next one
        SDLOG("StyleMiiU was loaded this boot: theme not applied until the next boot");
    } else {
        // the selector is shown at the first Wii U Menu start of a boot; later starts (back from a game) apply the
        // theme chosen there, or the one chosen in the config menu since. With shuffle on, every start gets a
        // random theme (the first one through the selector, like an autostart theme).
        static bool sSelected     = false;
        static bool sWithoutTheme = false;     // B in the selector
        static std::string sActiveAtSelection; // the config menu's theme then
        static bool sShuffleAtSelection = false;
        if (!sSelected) {
            RecoverInterruptedPackages(THEMES_PATH);
        }
        const auto themes = ListThemes();
        std::string theme;
        if (!sSelected) {
            sSelected           = true;
            theme               = SelectTheme(themes);
            sWithoutTheme       = theme.empty();
            sActiveAtSelection  = gActiveTheme;
            sShuffleAtSelection = gShuffleThemes;
        } else if (!themes.empty() && !(sWithoutTheme && gActiveTheme == sActiveAtSelection && gShuffleThemes == sShuffleAtSelection)) {
            if (gShuffleThemes) {
                theme = ShuffleTheme(themes);
            } else {
                theme = std::find(themes.begin(), themes.end(), gActiveTheme) != themes.end() ? gActiveTheme : themes.front();
            }
        }
        if (theme.empty()) {
            SDLOG("%s", themes.empty() ? "no themes" : "no theme this time");
        } else {
            if (gShuffleThemes) {
                RememberShuffledTheme(theme);
            }
            // music first: its one-time prep reads the console's own sound files, before any layer is in place
            if (gShuffleMusic) {
                PrepareMusic(themes);
            }
            ApplyTheme(theme);
            if (gShuffleMusic) {
                ApplyShuffledMusic(theme, themes);
            }
        }
    }
    SDLOG("ON_APPLICATION_START done");
}

ON_APPLICATION_ENDS() {
    if (IsWiiUMenu(OSGetTitleID())) {
        SDLOG("=== Wii U Menu ends");
        if (gShuffleMusic) {
            SaveMusicHistory(); // stored at the start, written now so it didn't hold the start up
        }
    }
    RemoveTheme();
    deinitLogging();
}
