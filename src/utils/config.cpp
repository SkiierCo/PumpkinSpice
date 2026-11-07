#include "config.h"
#include "globals.h"
#include "logger.h"
#include "theme.h"
#include <string>
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

            if (StyleMiiUInstalled()) {
                root.add(WUPSConfigItemStub::Create("Off while StyleMiiU is installed"));
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
            root.add(WUPSConfigItemStub::Create("Changes apply when the Wii U Menu restarts"));
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
    }

} // namespace

void InitStorageAndConfig() {
    WUPSStorageError err;
    if ((err = WUPSStorageAPI::GetOrStoreDefault(PUMPKINSPICE_ENABLED_STRING, gPumpkinSpiceEnabled, DEFAULT_PUMPKINSPICE_ENABLED)) != WUPS_STORAGE_ERROR_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to get or create \"%s\": %s (%d)", PUMPKINSPICE_ENABLED_STRING, WUPSStorageAPI_GetStatusStr(err), err);
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
