#include "globals.h"

bool gPumpkinSpiceEnabled = DEFAULT_PUMPKINSPICE_ENABLED;
bool gTitleFiles          = DEFAULT_TITLE_FILES;
bool gSkipPrepareScreen   = DEFAULT_SKIP_PREPARE_SCREEN;
std::string gAutostartTheme;
std::string gActiveTheme;
bool gShuffleThemes = DEFAULT_SHUFFLE_THEMES;
std::string gLastShuffledTheme;

std::vector<CRLayerHandle> gLayerHandles;
