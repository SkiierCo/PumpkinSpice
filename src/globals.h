#pragma once
#include "version.h"
#include <content_redirection/redirection.h>
#include <string>
#include <vector>

#define VERSION                      "v1.3"
#define VERSION_FULL                 VERSION VERSION_EXTRA

#define PUMPKINSPICE_ENABLED_STRING  "pumpkinSpiceEnabled"
#define ACTIVE_THEME_STRING          "activeTheme"
#define TITLE_FILES_STRING           "titleFiles"

#define DEFAULT_PUMPKINSPICE_ENABLED true
#define DEFAULT_TITLE_FILES          true

// Themes live in sd:/wiiu/pumpkinspice/themes/<theme name>/
#define THEMES_PATH                  "fs:/vol/external01/wiiu/pumpkinspice/themes"

extern bool gPumpkinSpiceEnabled;
extern bool gTitleFiles;         // load the theme's titles folder (app icons, EULA text)
extern std::string gActiveTheme; // folder name of the selected theme ("" = first theme found)

// layers added for the running Wii U Menu; removed when it ends
extern std::vector<CRLayerHandle> gLayerHandles;
