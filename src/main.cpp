#include "globals.h"
#include "theme.h"
#include "utils/config.h"
#include "utils/logger.h"
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
}

ON_APPLICATION_START() {
    initLogging();
    if (!IsWiiUMenu(OSGetTitleID())) {
        return;
    }
    if (gPumpkinSpiceEnabled) {
        ApplyTheme();
    } else {
        DEBUG_FUNCTION_LINE("PumpkinSpice is disabled");
    }
}

ON_APPLICATION_ENDS() {
    RemoveTheme();
    deinitLogging();
}
