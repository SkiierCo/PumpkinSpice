#pragma once
#include <string>
#include <vector>

// SDCafiine's modpack selector, for themes: shown when the Wii U Menu starts. Returns the theme to apply, or "" to
// start without a theme (B). A theme picked with A becomes the selected theme in the config menu too.
std::string SelectTheme(const std::vector<std::string> &themes);

// Shuffle: a random theme from `themes`, never the one shuffle applied last when there are 2 or more ("" if empty).
std::string ShuffleTheme(const std::vector<std::string> &themes);

// Remembers the theme shuffle applied, so the next shuffle skips it (stored, so it holds across boots too).
void RememberShuffledTheme(const std::string &theme);
