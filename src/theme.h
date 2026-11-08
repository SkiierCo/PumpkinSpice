#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Theme folder names in THEMES_PATH, sorted.
std::vector<std::string> ListThemes();

// Switches the StyleMiiU plugin (used by Themiify) off by renaming stylemiiu*.wps to .off next to PumpkinSpice's own
// .wps. Checked once per boot. True if StyleMiiU was found: it is still loaded until the next boot.
bool SwitchOffStyleMiiU();

// True if SwitchOffStyleMiiU found (and renamed) StyleMiiU this boot.
bool StyleMiiUSwitchedOffThisBoot();

// True for the Wii U Menu (JPN, USA, EUR).
bool IsWiiUMenu(uint64_t titleID);

// Applies the selected theme (or the first one found) to the running Wii U Menu.
void ApplyTheme();

// Removes every layer ApplyTheme added.
void RemoveTheme();
