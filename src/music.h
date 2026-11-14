#pragma once
#include <string>
#include <vector>

// Shuffle music: a random song at every Wii U Menu start, separate from the theme, with the theme's own sound effects.
// How it works is at the top of music.cpp.

// Gets every song and every theme's sound archive ready for Shuffle music, once (like a theme package): only what's
// new or changed since last time is done, with a screen while it runs. `themes` is ListThemes(). Call before
// ApplyTheme, while no layers are in place, so the console's own sound files can be read.
void PrepareMusic(const std::vector<std::string> &themes);

// Adds the layers that play a shuffled song over `theme` (already applied), or now and then (1 in 100) the console's
// own music and sound effects, with a notification. Does nothing for a theme that isn't ready yet. `themes` is
// ListThemes(): their songs are in the shuffle too.
void ApplyShuffledMusic(const std::string &theme, const std::vector<std::string> &themes);

// Saves the music history to the SD card (ApplyShuffledMusic only stores it, so the Menu start isn't held up by the
// write). Call when the Menu closes.
void SaveMusicHistory();
