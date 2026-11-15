#pragma once

void InitStorageAndConfig();

// True once after the config menu restarted the Wii U Menu itself (a setting changed); the flag is cleared by reading
// it. That start skips the theme selector even with "Theme selector at every Menu start" on.
bool TakeRestartedByPlugin();