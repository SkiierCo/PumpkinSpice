#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Theme folder names in THEMES_PATH, sorted.
std::vector<std::string> ListThemes();

// Switches the StyleMiiU plugin (used by Themiify) off by renaming stylemiiu*.wps to stylemiiu*.wps1 next to
// PumpkinSpice's own .wps. True if StyleMiiU was loaded at this boot: it stays loaded until the next boot.
bool SwitchOffStyleMiiU();

// Switches StyleMiiU back on (PumpkinSpice disabled): stylemiiu*.wps1 renamed back to .wps; loaded from the next boot.
void SwitchOnStyleMiiU();

// True if StyleMiiU was loaded at this boot (checked by the first SwitchOffStyleMiiU or SwitchOnStyleMiiU of a boot).
bool StyleMiiUSwitchedOffThisBoot();

// True for the Wii U Menu (JPN, USA, EUR).
bool IsWiiUMenu(uint64_t titleID);

// Gets a theme (a name from ListThemes) ready before it's applied: a theme package becomes its folder and BPS patches
// are applied, with a screen saying so, and a screen with the reason if that fails. Adds no layers, so call it before
// PrepareMusic, which then sees the theme's finished files. False if there's no theme folder to apply.
bool PrepareTheme(const std::string &theme);

// Applies a theme PrepareTheme got ready to the running Wii U Menu.
void ApplyTheme(const std::string &theme);

// Removes every layer ApplyTheme added.
void RemoveTheme();
