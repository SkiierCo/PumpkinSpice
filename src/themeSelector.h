#pragma once
#include <cstdint>
#include <string>
#include <vector>

// SDCafiine's modpack selector, for themes: shown when the Wii U Menu starts. Returns the theme to apply, or "" to
// start without a theme (B). A theme picked with A becomes the selected theme in the config menu too.
std::string SelectTheme(const std::vector<std::string> &themes);

// Shuffle: a random theme from `themes` ("" if empty). None of the last 5 themes shuffle applied can come back (with 5
// or fewer themes, none but the oldest), and after that older themes are more likely. Weights are in themeSelector.cpp.
std::string ShuffleTheme(const std::vector<std::string> &themes);

// Adds the theme shuffle applied to the history the next shuffle reads (stored, so it holds across boots too).
void RememberShuffledTheme(const std::string &theme);

// The same pick for anything (music shuffle uses it for songs): `history` is newest first, joined with '|'; `what`
// only names it in the SD log. RememberShuffled puts `item` at the front of `history` and stores it under `key`,
// saved to the SD card right away unless `save` is false (then the next save takes it along).
std::string ShufflePick(const std::vector<std::string> &items, const std::string &history, const char *what);
void RememberShuffled(const char *key, std::string &history, const std::string &item, bool save = true);

// A random number from 0 to n - 1, from the same generator (0 if n is 0).
uint32_t ShuffleRandom(uint32_t n);
