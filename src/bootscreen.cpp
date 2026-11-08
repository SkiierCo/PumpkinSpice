// SDCafiine's selector screen (modpackSelector.cpp ScreenInit/ScreenDeInit/console_print_pos and utils/input.cpp),
// kept as it was there.
#include "bootscreen.h"
#include "utils/logger.h"
#include <coreinit/cache.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory/mappedmemory.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>

namespace {
    uint8_t *sScreenBuffer0 = nullptr;
    uint8_t *sScreenBuffer1 = nullptr;
    uint32_t sScreenSize0   = 0;
    uint32_t sScreenSize1   = 0;

    uint32_t RemapWiiMoteButtons(uint32_t buttons) {
        uint32_t conv = 0;
        if (buttons & WPAD_BUTTON_LEFT) conv |= VPAD_BUTTON_LEFT;
        if (buttons & WPAD_BUTTON_RIGHT) conv |= VPAD_BUTTON_RIGHT;
        if (buttons & WPAD_BUTTON_DOWN) conv |= VPAD_BUTTON_DOWN;
        if (buttons & WPAD_BUTTON_UP) conv |= VPAD_BUTTON_UP;
        if (buttons & WPAD_BUTTON_PLUS) conv |= VPAD_BUTTON_PLUS;
        if (buttons & WPAD_BUTTON_B) conv |= VPAD_BUTTON_B;
        if (buttons & WPAD_BUTTON_A) conv |= VPAD_BUTTON_A;
        if (buttons & WPAD_BUTTON_MINUS) conv |= VPAD_BUTTON_MINUS;
        if (buttons & WPAD_BUTTON_HOME) conv |= VPAD_BUTTON_HOME;
        return conv;
    }

    uint32_t RemapClassicButtons(uint32_t buttons) {
        uint32_t conv = 0;
        if (buttons & WPAD_CLASSIC_BUTTON_LEFT) conv |= VPAD_BUTTON_LEFT;
        if (buttons & WPAD_CLASSIC_BUTTON_RIGHT) conv |= VPAD_BUTTON_RIGHT;
        if (buttons & WPAD_CLASSIC_BUTTON_DOWN) conv |= VPAD_BUTTON_DOWN;
        if (buttons & WPAD_CLASSIC_BUTTON_UP) conv |= VPAD_BUTTON_UP;
        if (buttons & WPAD_CLASSIC_BUTTON_PLUS) conv |= VPAD_BUTTON_PLUS;
        if (buttons & WPAD_CLASSIC_BUTTON_X) conv |= VPAD_BUTTON_X;
        if (buttons & WPAD_CLASSIC_BUTTON_Y) conv |= VPAD_BUTTON_Y;
        if (buttons & WPAD_CLASSIC_BUTTON_B) conv |= VPAD_BUTTON_B;
        if (buttons & WPAD_CLASSIC_BUTTON_A) conv |= VPAD_BUTTON_A;
        if (buttons & WPAD_CLASSIC_BUTTON_MINUS) conv |= VPAD_BUTTON_MINUS;
        if (buttons & WPAD_CLASSIC_BUTTON_HOME) conv |= VPAD_BUTTON_HOME;
        if (buttons & WPAD_CLASSIC_BUTTON_ZR) conv |= VPAD_BUTTON_ZR;
        if (buttons & WPAD_CLASSIC_BUTTON_ZL) conv |= VPAD_BUTTON_ZL;
        if (buttons & WPAD_CLASSIC_BUTTON_R) conv |= VPAD_BUTTON_R;
        if (buttons & WPAD_CLASSIC_BUTTON_L) conv |= VPAD_BUTTON_L;
        return conv;
    }
} // namespace

bool ScreenInit() {
    if (sScreenBuffer0 != nullptr) {
        return true; // already open
    }
    OSScreenInit();
    sScreenSize0   = OSScreenGetBufferSizeEx(SCREEN_TV);
    sScreenSize1   = OSScreenGetBufferSizeEx(SCREEN_DRC);
    sScreenBuffer0 = (uint8_t *) MEMAllocFromMappedMemoryForGX2Ex(sScreenSize0, 0x100);
    sScreenBuffer1 = (uint8_t *) MEMAllocFromMappedMemoryForGX2Ex(sScreenSize1, 0x100);
    if (sScreenBuffer0 == nullptr || sScreenBuffer1 == nullptr) {
        if (sScreenBuffer0) {
            MEMFreeToMappedMemory(sScreenBuffer0);
            sScreenBuffer0 = nullptr;
        }
        if (sScreenBuffer1) {
            MEMFreeToMappedMemory(sScreenBuffer1);
            sScreenBuffer1 = nullptr;
        }
        DEBUG_FUNCTION_LINE_ERR("Failed to allocate screenBuffer");
        return false;
    }
    OSScreenSetBufferEx(SCREEN_TV, (void *) sScreenBuffer0);
    OSScreenSetBufferEx(SCREEN_DRC, (void *) sScreenBuffer1);

    OSScreenEnableEx(SCREEN_TV, 1);
    OSScreenEnableEx(SCREEN_DRC, 1);

    ScreenClear();
    ScreenFlip();
    return true;
}

void ScreenDeInit() {
    if (sScreenBuffer0 == nullptr) {
        return;
    }
    ScreenClear();
    ScreenFlip();
    MEMFreeToMappedMemory(sScreenBuffer0);
    MEMFreeToMappedMemory(sScreenBuffer1);
    sScreenBuffer0 = nullptr;
    sScreenBuffer1 = nullptr;
}

bool ScreenIsOpen() {
    return sScreenBuffer0 != nullptr;
}

void ScreenClear() {
    OSScreenClearBufferEx(SCREEN_TV, 0);
    OSScreenClearBufferEx(SCREEN_DRC, 0);
}

void ScreenPrint(int x, int y, const char *format, ...) {
    char *tmp = nullptr;
    va_list va;
    va_start(va, format);
    if ((vasprintf(&tmp, format, va) >= 0) && tmp) {
        if (strlen(tmp) > 79) {
            tmp[79] = 0;
        }
        OSScreenPutFontEx(SCREEN_TV, x, y, tmp);
        OSScreenPutFontEx(SCREEN_DRC, x, y, tmp);
    }
    va_end(va);
    free(tmp);
}

void ScreenFlip() {
    DCFlushRange(sScreenBuffer0, sScreenSize0);
    DCFlushRange(sScreenBuffer1, sScreenSize1);
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

void InputInit() {
    KPADInit();
    WPADEnableURCC(true);
}

void InputDeInit() {
    KPADShutdown();
}

uint32_t ReadButtonsHeld() {
    for (int attempt = 0; attempt < 20; attempt++) {
        bool read        = false;
        uint32_t buttons = 0;
        VPADStatus vpad{};
        VPADReadError vpadError;
        if (VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError) > 0 && vpadError == VPAD_READ_SUCCESS) {
            buttons = vpad.hold;
            read    = true;
        }
        for (int i = 0; i < 4; i++) {
            KPADStatus kpad{};
            KPADError kpadError;
            if (KPADReadEx((KPADChan) i, &kpad, 1, &kpadError) > 0 && kpadError == KPAD_ERROR_OK && kpad.extensionType != 0xFF) {
                read = true;
                if (kpad.extensionType == WPAD_EXT_CORE || kpad.extensionType == WPAD_EXT_NUNCHUK) {
                    buttons |= RemapWiiMoteButtons(kpad.hold);
                } else {
                    buttons |= RemapClassicButtons(kpad.classic.hold);
                }
            }
        }
        if (read) {
            return buttons;
        }
        OSSleepTicks(OSMillisecondsToTicks(10));
    }
    return 0;
}

uint32_t ReadButtonsTriggered() {
    uint32_t buttons = 0;
    VPADStatus vpad{};
    VPADReadError vpadError;
    VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError);
    if (vpadError == VPAD_READ_SUCCESS) {
        buttons = vpad.trigger;
    }
    for (int i = 0; i < 4; i++) {
        KPADStatus kpad{};
        KPADError kpadError;
        if (KPADReadEx((KPADChan) i, &kpad, 1, &kpadError) > 0 && kpadError == KPAD_ERROR_OK && kpad.extensionType != 0xFF) {
            if (kpad.extensionType == WPAD_EXT_CORE || kpad.extensionType == WPAD_EXT_NUNCHUK) {
                buttons |= RemapWiiMoteButtons(kpad.trigger);
            } else {
                buttons |= RemapClassicButtons(kpad.classic.trigger);
            }
        }
    }
    return buttons;
}
