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

// Adds the layers that play `song` (a name from MusicSongs) over `theme` (already applied). Does nothing if that song or
// the theme isn't ready: the theme's own music plays. Needs PrepareMusic first, like ApplyShuffledMusic.
void ApplySelectedMusic(const std::string &theme, const std::string &song);

// True if `theme`'s own sound archive has a song of its own (the regular way or streamed), as the last PrepareMusic
// found it. False for a theme without one, which plays the default music.
bool ThemeHasOwnSong(const std::string &theme);

// The songs ready for Shuffle music and the Music list: this Menu start's if PrepareMusic ran, otherwise the ones the
// last prep left in index.txt. Empty if music was never prepared.
std::vector<std::string> MusicSongs();

// Saves the music history to the SD card (ApplyShuffledMusic only stores it, so the Menu start isn't held up by the
// write). Call when the Menu closes.
void SaveMusicHistory();
