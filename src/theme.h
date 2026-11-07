#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Theme folder names in THEMES_PATH, sorted.
std::vector<std::string> ListThemes();

// True when the StyleMiiU plugin (used by Themiify) is installed next to PumpkinSpice; PumpkinSpice then stays off.
bool StyleMiiUInstalled();

// True for the Wii U Menu (JPN, USA, EUR).
bool IsWiiUMenu(uint64_t titleID);

// Applies the selected theme (or the first one found) to the running Wii U Menu.
void ApplyTheme();

// Removes every layer ApplyTheme added.
void RemoveTheme();
