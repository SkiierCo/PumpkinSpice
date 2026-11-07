#pragma once
#include "version.h"
#include <content_redirection/redirection.h>
#include <string>
#include <vector>

#define VERSION                      "v1.1"
#define VERSION_FULL                 VERSION VERSION_EXTRA

#define PUMPKINSPICE_ENABLED_STRING  "pumpkinSpiceEnabled"
#define ACTIVE_THEME_STRING          "activeTheme"

#define DEFAULT_PUMPKINSPICE_ENABLED true

// Themes live in sd:/wiiu/pumpkinspice/themes/<theme name>/
#define THEMES_PATH                  "fs:/vol/external01/wiiu/pumpkinspice/themes"

extern bool gPumpkinSpiceEnabled;
extern std::string gActiveTheme; // folder name of the selected theme ("" = first theme found)

// layers added for the running Wii U Menu; removed when it ends
extern std::vector<CRLayerHandle> gLayerHandles;
