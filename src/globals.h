#pragma once
#include "version.h"
#include <content_redirection/redirection.h>
#include <string>
#include <vector>

#define VERSION                      "v1.5"
#define VERSION_FULL                 VERSION VERSION_EXTRA

#define PUMPKINSPICE_ENABLED_STRING  "pumpkinSpiceEnabled"
#define ACTIVE_THEME_STRING          "activeTheme"
#define TITLE_FILES_STRING           "titleFiles"
#define AUTOSTART_THEME_STRING       "autostartTheme"
#define SKIP_PREPARE_SCREEN_STRING   "skipPrepareScreen"
#define SHUFFLE_THEMES_STRING        "shuffleThemes"
#define SHUFFLE_HISTORY_STRING       "shuffleHistory"
#define LAST_SHUFFLED_THEME_STRING   "lastShuffledTheme" // v1.4.2's single last theme, only read to start the history
#define SHUFFLE_MUSIC_STRING         "shuffleMusic"
#define MUSIC_HISTORY_STRING         "musicHistory"

#define DEFAULT_PUMPKINSPICE_ENABLED true
#define DEFAULT_TITLE_FILES          true
#define DEFAULT_SKIP_PREPARE_SCREEN  false
#define DEFAULT_SHUFFLE_THEMES       false
#define DEFAULT_SHUFFLE_MUSIC        false

// Themes live in sd:/wiiu/pumpkinspice/themes/<theme name>/
#define THEMES_PATH                  "fs:/vol/external01/wiiu/pumpkinspice/themes"
// Songs for Shuffle music besides the themes' own: sd:/wiiu/pumpkinspice/music/<song name>/cafe_barista_men.bfsar
#define MUSIC_PATH                   "fs:/vol/external01/wiiu/pumpkinspice/music"
// What Shuffle music gets ready once (music.cpp)
#define MUSIC_CACHE_PATH             "fs:/vol/external01/wiiu/pumpkinspice/music-cache"

extern bool gPumpkinSpiceEnabled;
extern bool gTitleFiles;            // load the theme's titles folder (app icons, EULA text)
extern bool gSkipPrepareScreen;     // theme selector: start the autostart theme without "Preparing theme..."
extern std::string gAutostartTheme; // theme selector: theme started without the list ("" = off)
extern std::string gActiveTheme;    // folder name of the selected theme ("" = first theme found)
extern bool gShuffleThemes;         // a random theme at every Wii U Menu start instead of gActiveTheme / gAutostartTheme
extern std::string gShuffleHistory; // themes shuffle applied, newest first, joined with '|' (not allowed in FAT names)
extern bool gShuffleMusic;          // a random song at every Wii U Menu start, the theme's own sound effects kept
extern std::string gMusicHistory;   // songs music shuffle played, same form as gShuffleHistory

// layers added for the running Wii U Menu; removed when it ends
extern std::vector<CRLayerHandle> gLayerHandles;
