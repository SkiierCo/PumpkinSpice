#pragma once
#include <string>
#include <vector>

// SDCafiine's modpack selector, for themes: shown when the Wii U Menu starts. Returns the theme to apply, or "" to
// start without a theme (B). A theme picked with A becomes the selected theme in the config menu too.
std::string SelectTheme(const std::vector<std::string> &themes);
