#pragma once
#include <cstdint>

// Text on the TV and the GamePad while the Wii U Menu is starting, before it draws anything itself: SDCafiine's
// modpack selector screen, used for the theme selector and the messages while a theme package is prepared.
// The screen buffers come from Aroma's mapped memory (as in SDCafiine) and are freed again in ScreenDeInit.

bool ScreenInit(); // false if the buffers could not be allocated (nothing is shown then)
void ScreenDeInit();
bool ScreenIsOpen();
void ScreenClear();
void ScreenPrint(int x, int y, const char *format, ...) __attribute__((format(printf, 3, 4)));
void ScreenFlip();

// GamePad, Wii Remote and Classic Controller input as VPAD_BUTTON_* bits (SDCafiine's input.cpp)
void InputInit();
void InputDeInit();
// buttons pressed since the last read; `held` (optional) gets the buttons held in that same reading
uint32_t ReadButtonsTriggered(uint32_t *held = nullptr);
// buttons held right now; tries for up to 200 ms while the controllers have no reading yet (right at startup)
uint32_t ReadButtonsHeld();
