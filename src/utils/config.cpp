#include "config.h"
#include "globals.h"
#include "logger.h"
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
    std::string sThemeAtOpen; // the theme when the config menu was opened: changed -> the Wii U Menu restarts on close

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

            if (StyleMiiUSwitchedOffThisBoot()) {
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
            }
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
        // a new theme loads when the Wii U Menu starts: restart it now (in a game, it loads when the Menu comes back)
        if (gPumpkinSpiceEnabled && gActiveTheme != sThemeAtOpen && IsWiiUMenu(OSGetTitleID())) {
            SDLOG("config menu: theme changed to \"%s\", restarting the Wii U Menu", gActiveTheme.c_str());
            SYSLaunchMenu();
        }
    }

} // namespace

void InitStorageAndConfig() {
    WUPSStorageError err;
    if ((err = WUPSStorageAPI::GetOrStoreDefault(PUMPKINSPICE_ENABLED_STRING, gPumpkinSpiceEnabled, DEFAULT_PUMPKINSPICE_ENABLED)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", PUMPKINSPICE_ENABLED_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(TITLE_FILES_STRING, gTitleFiles, DEFAULT_TITLE_FILES)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", TITLE_FILES_STRING, WUPSStorageAPI_GetStatusStr(err), err);
    }
    if ((err = WUPSStorageAPI::GetOrStoreDefault(SKIP_PREPARE_SCREEN_STRING, gSkipPrepareScreen, DEFAULT_SKIP_PREPARE_SCREEN)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", SKIP_PREPARE_SCREEN_STRING, WUPSStorageAPI_GetStatusStr(err), err);
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
