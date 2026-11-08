#include "globals.h"
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
        DEBUG_FUNCTION_LINE("PumpkinSpice is disabled"); // StyleMiiU is left alone
    } else if (SwitchOffStyleMiiU()) {
        // renamed now, but loaded since boot: the two would clash this boot, the theme applies from the next one
        SDLOG("StyleMiiU was loaded this boot: theme not applied until the next boot");
    } else {
        // the selector is shown at the first Wii U Menu start of a boot; later starts (back from a game) apply the
        // theme chosen there, or the one chosen in the config menu since
        static bool sSelected     = false;
        static bool sWithoutTheme = false;     // B in the selector
        static std::string sActiveAtSelection; // the config menu's theme then
        if (!sSelected) {
            RecoverInterruptedPackages(THEMES_PATH);
        }
        const auto themes = ListThemes();
        std::string theme;
        if (!sSelected) {
            sSelected          = true;
            theme              = SelectTheme(themes);
            sWithoutTheme      = theme.empty();
            sActiveAtSelection = gActiveTheme;
        } else if (!themes.empty() && !(sWithoutTheme && gActiveTheme == sActiveAtSelection)) {
            theme = std::find(themes.begin(), themes.end(), gActiveTheme) != themes.end() ? gActiveTheme : themes.front();
        }
        if (theme.empty()) {
            SDLOG("%s", themes.empty() ? "no themes" : "no theme this time");
        } else {
            ApplyTheme(theme);
        }
    }
    SDLOG("ON_APPLICATION_START done");
}

ON_APPLICATION_ENDS() {
    if (IsWiiUMenu(OSGetTitleID())) {
        SDLOG("=== Wii U Menu ends");
    }
    RemoveTheme();
    deinitLogging();
}
