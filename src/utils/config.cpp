#include "config.h"
#include "globals.h"
#include "logger.h"
#include "music.h"
#include "sdlog.h"
#include "theme.h"
#include <coreinit/title.h>
#include <string>
#include <string_view>
#include <sysapp/launch.h>
#include <vector>
#include <wups/config.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/config_api.h>
#include <wups/storage.h>

namespace {

    // theme list shown in the config menu; kept alive while the menu is open (the items refer to these strings)
    std::vector<std::string> sThemeNames;
    std::vector<WUPSConfigItemMultipleValues::ValuePair> sThemePairs;
    // the settings when the config menu was opened. If one of these changed the Wii U Menu restarts on close because
    // they only take effect when the Menu starts (autostart and skip screen don't count, they're only used at the
    // first Menu start of a boot)
    std::string sThemeAtOpen;
    bool sShuffleAtOpen    = false;
    bool sEnabledAtOpen    = false;
    bool sTitleFilesAtOpen = false;
    bool sMusicAtOpen      = false;
    std::string sSelectedMusicAtOpen;
    bool sThemeMusicFirstAtOpen = false;

    // Music priority: the song from the Music list, or a theme's own song
    const std::vector<WUPSConfigItemMultipleValues::ValuePair> kPriorityPairs = {{0, "Selected song"}, {1, "Theme"}};

    void priority_callback(ConfigItemMultipleValues *item, uint32_t newValue) {
        if (!item || newValue > 1) {
            return;
        }
        gThemeMusicFirst = newValue == 1;
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(THEME_MUSIC_FIRST_STRING, gThemeMusicFirst)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store the music priority: %s (%d)", WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    // the Music list: "Theme's own", then the songs music prep has ready (MusicSongs)
    std::vector<std::string> sMusicNames;
    std::vector<WUPSConfigItemMultipleValues::ValuePair> sMusicPairs;

    void music_callback(ConfigItemMultipleValues *item, uint32_t newValue) {
        if (!item || newValue > sMusicNames.size()) {
            return;
        }
        gSelectedMusic = newValue == 0 ? std::string() : sMusicNames[newValue - 1];
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(SELECTED_MUSIC_STRING, gSelectedMusic)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store the music: %s (%d)", WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    void shuffle_music_callback(ConfigItemBoolean *item, bool newValue) {
        if (!item || !item->identifier) {
            return;
        }
        gShuffleMusic = newValue;
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(item->identifier, newValue)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store \"%s\": %s (%d)", item->identifier, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    void shuffle_callback(ConfigItemBoolean *item, bool newValue) {
        if (!item || !item->identifier) {
            return;
        }
        gShuffleThemes = newValue;
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(item->identifier, newValue)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store \"%s\": %s (%d)", item->identifier, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    void enabled_callback(ConfigItemBoolean *item, bool newValue) {
        if (!item || !item->identifier) {
            return;
        }
        gPumpkinSpiceEnabled = newValue;
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(item->identifier, newValue)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store \"%s\": %s (%d)", item->identifier, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    void title_files_callback(ConfigItemBoolean *item, bool newValue) {
        if (!item || !item->identifier) {
            return;
        }
        gTitleFiles = newValue;
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(item->identifier, newValue)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store \"%s\": %s (%d)", item->identifier, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    // the theme selector's settings (themeSelector.cpp)
    std::vector<WUPSConfigItemMultipleValues::ValuePair> sAutostartPairs; // "Off", then the themes

    void autostart_callback(ConfigItemMultipleValues *item, uint32_t newValue) {
        if (!item || newValue > sThemeNames.size()) {
            return;
        }
        gAutostartTheme = newValue == 0 ? std::string() : sThemeNames[newValue - 1];
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(AUTOSTART_THEME_STRING, gAutostartTheme)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store the autostart theme: %s (%d)", WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    bool sRestartedByPlugin = false; // set just before the config menu restarts the Wii U Menu (TakeRestartedByPlugin)

    void selector_every_start_callback(ConfigItemBoolean *item, bool newValue) {
        if (!item || !item->identifier) {
            return;
        }
        gSelectorEveryStart = newValue;
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(item->identifier, newValue)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store \"%s\": %s (%d)", item->identifier, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    void skip_prepare_callback(ConfigItemBoolean *item, bool newValue) {
        if (!item || !item->identifier) {
            return;
        }
        gSkipPrepareScreen = newValue;
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(item->identifier, newValue)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store \"%s\": %s (%d)", item->identifier, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    void theme_callback(ConfigItemMultipleValues *item, uint32_t newValue) {
        if (!item || newValue >= sThemeNames.size()) {
            return;
        }
        gActiveTheme = sThemeNames[newValue];
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::Store(ACTIVE_THEME_STRING, gActiveTheme)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Failed to store the theme: %s (%d)", WUPSStorageAPI_GetStatusStr(err), err);
        }
    }

    WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle) {
        try {
            WUPSConfigCategory root = WUPSConfigCategory(rootHandle);
            sThemeAtOpen            = gActiveTheme;
            sShuffleAtOpen          = gShuffleThemes;
            sEnabledAtOpen          = gPumpkinSpiceEnabled;
            sTitleFilesAtOpen       = gTitleFiles;
            sMusicAtOpen            = gShuffleMusic;
            sSelectedMusicAtOpen    = gSelectedMusic;
            sThemeMusicFirstAtOpen  = gThemeMusicFirst;

            if (gPumpkinSpiceEnabled && StyleMiiUSwitchedOffThisBoot()) {
                root.add(WUPSConfigItemStub::Create("StyleMiiU switched off: theme applies after a reboot"));
            }
            root.add(WUPSConfigItemBoolean::Create(PUMPKINSPICE_ENABLED_STRING, "Enabled",
                                                   DEFAULT_PUMPKINSPICE_ENABLED, gPumpkinSpiceEnabled,
                                                   &enabled_callback));

            sThemeNames = ListThemes();
            sThemePairs.clear();
            if (sThemeNames.empty()) {
                root.add(WUPSConfigItemStub::Create("No themes in sd:/wiiu/pumpkinspice/themes"));
            } else {
                int current = 0;
                for (size_t i = 0; i < sThemeNames.size(); i++) {
                    sThemePairs.push_back({static_cast<uint32_t>(i), sThemeNames[i]});
                    if (sThemeNames[i] == gActiveTheme) {
                        current = static_cast<int>(i);
                    }
                }
                root.add(WUPSConfigItemMultipleValues::CreateFromIndex(ACTIVE_THEME_STRING, "Theme",
                                                                       0, current, sThemePairs, &theme_callback));

                // not used while Shuffle music is on (shuffle picks the song), but it stays in the list with its value.
                // A chosen song that isn't ready any more shows as "Theme's own" and stays stored until another pick.
                sMusicNames = MusicSongs();
                sMusicPairs.clear();
                sMusicPairs.push_back({0, "Theme's own"});
                int music = 0;
                for (size_t i = 0; i < sMusicNames.size(); i++) {
                    sMusicPairs.push_back({static_cast<uint32_t>(i + 1), sMusicNames[i]});
                    if (sMusicNames[i] == gSelectedMusic) {
                        music = static_cast<int>(i + 1);
                    }
                }
                root.add(WUPSConfigItemMultipleValues::CreateFromIndex(SELECTED_MUSIC_STRING, "Music",
                                                                       0, music, sMusicPairs, &music_callback));
                root.add(WUPSConfigItemBoolean::Create(SHUFFLE_THEMES_STRING, "Shuffle themes", DEFAULT_SHUFFLE_THEMES,
                                                       gShuffleThemes, &shuffle_callback));
            }
            root.add(WUPSConfigItemBoolean::Create(SHUFFLE_MUSIC_STRING, "Shuffle music", DEFAULT_SHUFFLE_MUSIC, gShuffleMusic,
                                                   &shuffle_music_callback));
            root.add(WUPSConfigItemBoolean::Create(TITLE_FILES_STRING, "App icons and EULA", DEFAULT_TITLE_FILES, gTitleFiles,
                                                   &title_files_callback));
            root.add(WUPSConfigItemStub::Create("Changes apply when the Wii U Menu restarts"));

            auto advancedSettings = WUPSConfigCategory::Create("Advanced settings");
            sAutostartPairs.clear();
            sAutostartPairs.push_back({0, "Off"});
            int autostart = 0;
            for (size_t i = 0; i < sThemeNames.size(); i++) {
                sAutostartPairs.push_back({static_cast<uint32_t>(i + 1), sThemeNames[i]});
                if (sThemeNames[i] == gAutostartTheme) {
                    autostart = static_cast<int>(i + 1);
                }
            }
            advancedSettings.add(WUPSConfigItemMultipleValues::CreateFromIndex(AUTOSTART_THEME_STRING, "Autostart theme",
                                                                               0, autostart, sAutostartPairs, &autostart_callback));
            advancedSettings.add(WUPSConfigItemBoolean::Create(SKIP_PREPARE_SCREEN_STRING,
                                                               "Skip \"Preparing theme...\" screen",
                                                               DEFAULT_SKIP_PREPARE_SCREEN, gSkipPrepareScreen,
                                                               &skip_prepare_callback));
            // not used while Shuffle themes is on (shuffle picks the theme), but it stays in the list with its value
            advancedSettings.add(WUPSConfigItemBoolean::Create(SELECTOR_EVERY_START_STRING, "Theme selector at every Menu start",
                                                               DEFAULT_SELECTOR_EVERY_START, gSelectorEveryStart,
                                                               &selector_every_start_callback));
            // only used with a song picked in the Music list and Shuffle music off, but it stays in the list
            advancedSettings.add(WUPSConfigItemMultipleValues::CreateFromIndex(THEME_MUSIC_FIRST_STRING, "Music priority", 0,
                                                                               gThemeMusicFirst ? 1 : 0, kPriorityPairs, &priority_callback));
            root.add(std::move(advancedSettings));
        } catch (std::exception &e) {
            DEBUG_FUNCTION_LINE_ERR("Exception: %s\n", e.what());
            return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
        }
        return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
    }

    void ConfigMenuClosedCallback() {
        WUPSStorageError err;
        if ((err = WUPSStorageAPI::SaveStorage()) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to save storage: %s (%d)", WUPSStorageAPI_GetStatusStr(err), err);
        }
        // disabled: StyleMiiU back on now, so it loads at the next boot (also done at the next Wii U Menu start)
        if (!gPumpkinSpiceEnabled) {
            SwitchOnStyleMiiU();
        }
        // these settings only take effect when the Wii U Menu starts so it restarts now (in a game they apply when the
        // Menu comes back). Turning PumpkinSpice on or off always counts and the others only count while it's on. While
        // shuffling the Theme setting isn't used so changing it doesn't restart anything
        const bool enabledChanged    = gPumpkinSpiceEnabled != sEnabledAtOpen;
        const bool themeChanged      = gPumpkinSpiceEnabled && !gShuffleThemes && gActiveTheme != sThemeAtOpen;
        const bool shuffleChanged    = gPumpkinSpiceEnabled && gShuffleThemes != sShuffleAtOpen;
        const bool titleFilesChanged = gPumpkinSpiceEnabled && gTitleFiles != sTitleFilesAtOpen;
        const bool musicChanged      = gPumpkinSpiceEnabled && gShuffleMusic != sMusicAtOpen;
        const bool songChanged       = gPumpkinSpiceEnabled && !gShuffleMusic && gSelectedMusic != sSelectedMusicAtOpen;
        const bool priorityChanged   = gPumpkinSpiceEnabled && !gShuffleMusic && !gSelectedMusic.empty() && gThemeMusicFirst != sThemeMusicFirstAtOpen;
        if ((enabledChanged || themeChanged || shuffleChanged || titleFilesChanged || musicChanged || songChanged || priorityChanged) &&
            IsWiiUMenu(OSGetTitleID())) {
            SDLOG("config menu: restarting the Wii U Menu (enabled %d, theme \"%s\", shuffle %d, music %d, song \"%s\", theme music first %d, app icons and EULA %d)",
                  gPumpkinSpiceEnabled, gActiveTheme.c_str(), gShuffleThemes, gShuffleMusic, gSelectedMusic.c_str(), gThemeMusicFirst, gTitleFiles);
            sRestartedByPlugin = true; // the next start skips the theme selector: the settings just chose the theme
            SYSLaunchMenu();
        }
    }

} // namespace

bool TakeRestartedByPlugin() {
    const bool was     = sRestartedByPlugin;
    sRestartedByPlugin = false;
    return was;
}

void InitStorageAndConfig() {
    WUPSStorageError err;
    if ((err = WUPSStorageAPI::GetOrStoreDefault(SELECTOR_EVERY_START_STRING, gSelectorEveryStart, DEFAULT_SELECTOR_EVERY_START)) !=
        WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", SELECTOR_EVERY_START_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(PUMPKINSPICE_ENABLED_STRING, gPumpkinSpiceEnabled, DEFAULT_PUMPKINSPICE_ENABLED)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", PUMPKINSPICE_ENABLED_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(TITLE_FILES_STRING, gTitleFiles, DEFAULT_TITLE_FILES)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", TITLE_FILES_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(SKIP_PREPARE_SCREEN_STRING, gSkipPrepareScreen, DEFAULT_SKIP_PREPARE_SCREEN)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", SKIP_PREPARE_SCREEN_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(SHUFFLE_THEMES_STRING, gShuffleThemes, DEFAULT_SHUFFLE_THEMES)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", SHUFFLE_THEMES_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if (WUPSStorageAPI::Get(SHUFFLE_HISTORY_STRING, gShuffleHistory) != WUPS_STORAGE_ERROR_SUCCESS) {
        // stored the first time shuffle applies a theme. Coming from the first v1.4.2 build its one last theme
        // starts the history
        if (WUPSStorageAPI::Get(LAST_SHUFFLED_THEME_STRING, gShuffleHistory) != WUPS_STORAGE_ERROR_SUCCESS) {
            gShuffleHistory.clear();
        }
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(SHUFFLE_MUSIC_STRING, gShuffleMusic, DEFAULT_SHUFFLE_MUSIC)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", SHUFFLE_MUSIC_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if (WUPSStorageAPI::Get(MUSIC_HISTORY_STRING, gMusicHistory) != WUPS_STORAGE_ERROR_SUCCESS) {
        gMusicHistory.clear(); // stored the first time music shuffle plays a song
    }
    if (WUPSStorageAPI::Get(SELECTED_MUSIC_STRING, gSelectedMusic) != WUPS_STORAGE_ERROR_SUCCESS) {
        gSelectedMusic.clear(); // the theme's own, stored the first time a song is picked
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(THEME_MUSIC_FIRST_STRING, gThemeMusicFirst, DEFAULT_THEME_MUSIC_FIRST)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", THEME_MUSIC_FIRST_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if (WUPSStorageAPI::Get(AUTOSTART_THEME_STRING, gAutostartTheme) != WUPS_STORAGE_ERROR_SUCCESS) {
        gAutostartTheme.clear(); // off
        if ((err = WUPSStorageAPI::Store(AUTOSTART_THEME_STRING, gAutostartTheme)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to create \"%s\": %s (%d)", AUTOSTART_THEME_STRING, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }
    if (WUPSStorageAPI::Get(ACTIVE_THEME_STRING, gActiveTheme) != WUPS_STORAGE_ERROR_SUCCESS) {
        gActiveTheme.clear();
        if ((err = WUPSStorageAPI::Store(ACTIVE_THEME_STRING, gActiveTheme)) != WUPS_STORAGE_ERROR_SUCCESS) {
            DEBUG_FUNCTION_LINE_ERR("Failed to create \"%s\": %s (%d)", ACTIVE_THEME_STRING, WUPSStorageAPI_GetStatusStr(err), err);
        }
    }
    if ((err = WUPSStorageAPI::SaveStorage()) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to save storage: %s (%d)", WUPSStorageAPI_GetStatusStr(err), err);
    }

    WUPSConfigAPIOptionsV1 configOptions = {.name = "PumpkinSpice"};
    WUPSConfigAPIStatus configErr;
    if ((configErr = WUPSConfigAPI_Init(configOptions, ConfigMenuOpenedCallback, ConfigMenuClosedCallback)) != WUPSCONFIG_API_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init config api: %s (%d)", WUPSConfigAPI_GetStatusStr(configErr), configErr);
    }
}
