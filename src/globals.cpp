#include "globals.h"

bool gPumpkinSpiceEnabled = DEFAULT_PUMPKINSPICE_ENABLED;
bool gTitleFiles          = DEFAULT_TITLE_FILES;
bool gSkipPrepareScreen   = DEFAULT_SKIP_PREPARE_SCREEN;
std::string gAutostartTheme;
std::string gActiveTheme;
bool gShuffleThemes = DEFAULT_SHUFFLE_THEMES;
std::string gShuffleHistory;
bool gShuffleMusic = DEFAULT_SHUFFLE_MUSIC;
std::string gMusicHistory;
bool gSelectorEveryStart = DEFAULT_SELECTOR_EVERY_START;
std::string gSelectedMusic;
bool gThemeMusicFirst = DEFAULT_THEME_MUSIC_FIRST;

std::vector<CRLayerHandle> gLayerHandles;
