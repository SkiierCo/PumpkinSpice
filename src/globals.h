#pragma once
#include "version.h"
#include <content_redirection/redirection.h>
#include <string>
#include <vector>

#define VERSION                      "v1.4"
#define VERSION_FULL                 VERSION VERSION_EXTRA

#define PUMPKINSPICE_ENABLED_STRING  "pumpkinSpiceEnabled"
#define ACTIVE_THEME_STRING          "activeTheme"
#define TITLE_FILES_STRING           "titleFiles"
#define AUTOSTART_THEME_STRING       "autostartTheme"
#define SKIP_PREPARE_SCREEN_STRING   "skipPrepareScreen"

#define DEFAULT_PUMPKINSPICE_ENABLED true
#define DEFAULT_TITLE_FILES          true
#define DEFAULT_SKIP_PREPARE_SCREEN  false

// Themes live in sd:/wiiu/pumpkinspice/themes/<theme name>/
#define THEMES_PATH                  "fs:/vol/external01/wiiu/pumpkinspice/themes"

extern bool gPumpkinSpiceEnabled;
extern bool gTitleFiles;            // load the theme's titles folder (app icons, EULA text)
extern bool gSkipPrepareScreen;     // theme selector: start the autostart theme without "Preparing theme..."
extern std::string gAutostartTheme; // theme selector: theme started without the list ("" = off)
extern std::string gActiveTheme;    // folder name of the selected theme ("" = first theme found)

// layers added for the running Wii U Menu; removed when it ends
extern std::vector<CRLayerHandle> gLayerHandles;
