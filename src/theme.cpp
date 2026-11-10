#include "theme.h"
#include "bootscreen.h"
#include "codepatch.h"
#include "globals.h"
#include "package.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include <algorithm>
#include <cctype>
#include <content_redirection/redirection.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <vpad/input.h>

namespace {

    bool IsDirectory(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool IsFile(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
    }

    // Sub-directory names of `path`, without "." and "..", sorted.
    std::vector<std::string> ListDirs(const std::string &path) {
        std::vector<std::string> out;
        DIR *dir = opendir(path.c_str());
        if (!dir) {
            return out;
        }
        while (auto *entry = readdir(dir)) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            if (IsDirectory(path + "/" + name)) {
                out.push_back(name);
            }
        }
        closedir(dir);
        std::sort(out.begin(), out.end());
        return out;
    }

    bool AddLayer(CRLayerHandle &handle, bool ok, const std::string &what) {
        if (!ok) {
            DEBUG_FUNCTION_LINE_ERR("Failed to redirect %s", what.c_str());
            return false;
        }
        gLayerHandles.push_back(handle);
        DEBUG_FUNCTION_LINE("Redirected %s", what.c_str());
        return true;
    }

    // <theme>/content -> the Menu's /vol/content (merged: files the theme lacks come from the Menu)
    void AddContentLayer(const std::string &themePath) {
        const std::string dir = themePath + "/content";
        if (!IsDirectory(dir)) {
            return;
        }
        CRLayerHandle handle = 0;
        auto res             = ContentRedirection_AddFSLayer(&handle, "PumpkinSpice /vol/content", dir.c_str(), FS_LAYER_TYPE_CONTENT_MERGE);
        AddLayer(handle, res == CONTENT_REDIRECTION_RESULT_SUCCESS, "/vol/content to " + dir);
    }

    bool IsHex(const std::string &s, size_t len) {
        return s.size() == len && s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
    }

    std::string Upper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return s;
    }

    // /vol/storage_mlc01/<sys|usr>/title/<high>: system titles (00050010 system apps, 0005001B system data, 00050030
    // applets) are in sys, others in usr
    std::string TitleGroupPath(const std::string &high) {
        const bool system = high.rfind("0005001", 0) == 0 || high == "00050030";
        return std::string("/vol/storage_mlc01/") + (system ? "sys" : "usr") + "/title/" + high;
    }

    void AddMergeLayer(const std::string &name, const std::string &target, const std::string &replacement) {
        CRLayerHandle handle = 0;
        auto res             = ContentRedirection_AddFSLayerEx(&handle, name.c_str(), target.c_str(), replacement.c_str(), FS_LAYER_TYPE_EX_MERGE_DIRECTORY);
        AddLayer(handle, res == CONTENT_REDIRECTION_RESULT_SUCCESS, target + " to " + replacement);
    }

    // Files of other titles that the Menu reads itself, e.g. the EULA text (0005001B 10050000/content) and every app's
    // icon (<title>/meta/iconTex.tga), merged over the system's own. Two layouts of <theme>/titles:
    //   <8-digit high>/<8-digit low>/<folder>  one layer per high group (/vol/storage_mlc01/<sys|usr>/title/<high>)
    //   <16-digit title ID>/<folder>            one layer per title folder (the v1.0 layout)
    // Every merge layer opens its own FSA client in the Menu; on a console with many plugins 15 of them left the Menu
    // without a system handle (it halts when a system setting can't be read), so new themes use the group layout: a
    // theme with apps, applets and the EULA needs 3 layers instead of one per title.
    // Needs AddFSLayerEx (ContentRedirectionModule API version 2).
    void AddTitleLayers(const std::string &themePath) {
        const std::string titlesPath = themePath + "/titles";
        if (!IsDirectory(titlesPath)) {
            return;
        }
        ContentRedirectionVersion version = 0;
        if (ContentRedirection_GetVersion(&version) != CONTENT_REDIRECTION_RESULT_SUCCESS || version < 2) {
            DEBUG_FUNCTION_LINE_ERR("ContentRedirectionModule API version %d: the titles folder needs version 2 or newer", version);
            return;
        }
        const auto ids        = ListDirs(titlesPath);
        const bool groupStyle = std::any_of(ids.begin(), ids.end(), [](const std::string &id) { return IsHex(id, 8); });
        for (const auto &id : ids) {
            if (IsHex(id, 8)) {
                const std::string high = Upper(id);
                AddMergeLayer("PumpkinSpice titles " + high, TitleGroupPath(high), titlesPath + "/" + id);
            } else if (IsHex(id, 16) && groupStyle) {
                // left over from the old layout (e.g. a new theme version unpacked over an old one): its files would
                // load twice and every folder would cost another layer
                SDLOG("titles/%s skipped: this theme uses title groups (titles/<8-digit>/...)", id.c_str());
                DEBUG_FUNCTION_LINE_WARN("Skipping %s/%s: old layout next to title groups", titlesPath.c_str(), id.c_str());
            } else if (IsHex(id, 16)) {
                const std::string upper = Upper(id);
                const std::string base  = TitleGroupPath(upper.substr(0, 8)) + "/" + upper.substr(8) + "/";
                for (const auto &folder : ListDirs(titlesPath + "/" + id)) {
                    AddMergeLayer("PumpkinSpice " + upper + "/" + folder, base + folder, titlesPath + "/" + id + "/" + folder);
                }
            } else {
                DEBUG_FUNCTION_LINE_WARN("Skipping %s/%s: not an 8-digit title group or a 16-digit title ID", titlesPath.c_str(), id.c_str());
            }
        }
    }

} // namespace

// Theme folders and packages (<name>.ps, listed as <name>), sorted; a name that is both appears once.
std::vector<std::string> ListThemes() {
    std::vector<std::string> out = ListDirs(THEMES_PATH);
    // <name>.building / <name>.replaced: a package being turned into a theme folder, and the old folder it replaces
    // (left behind only by an interrupted first start)
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const std::string &n) {
                                 return n.size() > 9 && (n.compare(n.size() - 9, 9, ".building") == 0 ||
                                                         n.compare(n.size() - 9, 9, ".replaced") == 0);
                             }),
              out.end());
    if (DIR *dir = opendir(THEMES_PATH)) {
        while (auto *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name.size() > 3 && Upper(name.substr(name.size() - 3)) == ".PS" && IsFile(std::string(THEMES_PATH) + "/" + name)) {
                const std::string theme = name.substr(0, name.size() - 3);
                if (std::find(out.begin(), out.end(), theme) == out.end()) {
                    out.push_back(theme);
                }
            }
        }
        closedir(dir);
    }
    std::sort(out.begin(), out.end());
    return out;
}

namespace {
    int sStyleMiiUAtBoot = -1; // -1 not checked yet this boot, 0 no, 1 stylemiiu*.wps was there (loaded this boot)

    std::string Lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    bool IsWps(const std::string &lowerName, const char *prefix) {
        const size_t n = strlen(prefix);
        return lowerName.size() >= n + 4 && lowerName.compare(0, n, prefix) == 0 &&
               lowerName.compare(lowerName.size() - 4, 4, ".wps") == 0;
    }

    // stylemiiu*.wps1 .. stylemiiu*.wps9: StyleMiiU as SwitchOffStyleMiiU leaves it
    bool IsSwitchedOffStyleMiiU(const std::string &lowerName) {
        return !lowerName.empty() && lowerName.back() >= '1' && lowerName.back() <= '9' &&
               IsWps(lowerName.substr(0, lowerName.size() - 1), "stylemiiu");
    }

    using PluginFolders = std::vector<std::pair<std::string, std::vector<std::string>>>;

    // Every environment's plugins folder that holds PumpkinSpice's own .wps, with the file names in it (sorted).
    PluginFolders PluginFoldersWithPumpkinSpice() {
        PluginFolders out;
        const std::string envs = "fs:/vol/external01/wiiu/environments";
        for (const auto &env : ListDirs(envs)) {
            const std::string plugins = envs + "/" + env + "/plugins";
            DIR *dir                  = opendir(plugins.c_str());
            if (!dir) {
                continue;
            }
            bool ours = false;
            std::vector<std::string> names;
            while (auto *entry = readdir(dir)) {
                names.emplace_back(entry->d_name);
                if (IsWps(Lower(entry->d_name), "pumpkinspice")) {
                    ours = true;
                }
            }
            closedir(dir);
            if (ours) {
                std::sort(names.begin(), names.end());
                out.emplace_back(plugins, std::move(names));
            }
        }
        return out;
    }

    // The first look of a boot, before anything is renamed, tells whether Aroma loaded StyleMiiU at this boot.
    void CheckStyleMiiUAtBoot(const PluginFolders &folders) {
        if (sStyleMiiUAtBoot >= 0) {
            return;
        }
        sStyleMiiUAtBoot = 0;
        for (const auto &folder : folders) {
            for (const auto &name : folder.second) {
                if (IsWps(Lower(name), "stylemiiu")) {
                    sStyleMiiUAtBoot = 1;
                }
            }
        }
    }
} // namespace

// StyleMiiU (the plugin Themiify installs themes for) themes the Wii U Menu too, and the two clash, so PumpkinSpice
// switches it off: in every environment's plugins folder that also holds PumpkinSpice's own .wps, stylemiiu*.wps is
// renamed to <name>1 (stylemiiu.wps1: Aroma only loads .wps files; names compared without case; if that name is taken,
// the next free <name>2 .. <name>9 is used, so no file is overwritten).
// Aroma loads plugins at boot, so StyleMiiU stays loaded until the next one: true means it was loaded at this boot.
bool SwitchOffStyleMiiU() {
    const auto folders = PluginFoldersWithPumpkinSpice();
    CheckStyleMiiUAtBoot(folders);
    for (const auto &folder : folders) {
        for (const auto &name : folder.second) {
            if (!IsWps(Lower(name), "stylemiiu")) {
                continue;
            }
            const std::string from = folder.first + "/" + name;
            std::string to         = from + "1";
            for (int i = 2; IsFile(to) && i < 10; i++) {
                to = from + std::to_string(i);
            }
            [[maybe_unused]] const bool ok = !IsFile(to) && rename(from.c_str(), to.c_str()) == 0; // only logged
            SDLOG("StyleMiiU found: %s -> %s %s", from.c_str(), to.c_str(), ok ? "renamed" : "NOT renamed");
            DEBUG_FUNCTION_LINE("StyleMiiU %s: %s", ok ? "switched off" : "could not be renamed", from.c_str());
        }
    }
    return sStyleMiiUAtBoot == 1;
}

// PumpkinSpice disabled: StyleMiiU switched back on, stylemiiu*.wps1 (..9) renamed back to .wps unless a file of that
// name is already there. Aroma loads it from the next boot.
void SwitchOnStyleMiiU() {
    const auto folders = PluginFoldersWithPumpkinSpice();
    CheckStyleMiiUAtBoot(folders);
    for (const auto &folder : folders) {
        for (const auto &name : folder.second) {
            if (!IsSwitchedOffStyleMiiU(Lower(name))) {
                continue;
            }
            const std::string from         = folder.first + "/" + name;
            const std::string to           = from.substr(0, from.size() - 1);
            [[maybe_unused]] const bool ok = !IsFile(to) && rename(from.c_str(), to.c_str()) == 0; // only logged
            SDLOG("StyleMiiU switched back on: %s -> %s %s", from.c_str(), to.c_str(), ok ? "renamed" : "NOT renamed");
            DEBUG_FUNCTION_LINE("StyleMiiU %s: %s", ok ? "switched back on" : "could not be renamed back", from.c_str());
        }
    }
}

bool StyleMiiUSwitchedOffThisBoot() {
    return sStyleMiiUAtBoot == 1;
}

bool IsWiiUMenu(uint64_t titleID) {
    return titleID == 0x0005001010040000ULL || titleID == 0x0005001010040100ULL || titleID == 0x0005001010040200ULL;
}

namespace {
    // the screens while a package is prepared (bootscreen.h), in the style of SDCafiine's "Preparing modpack" screen
    void DrawPreparing(const std::string &theme, int file, int files) {
        ScreenClear();
        ScreenPrint(-2, -1, "PumpkinSpice " VERSION_FULL);
        ScreenPrint(-2, 1, "Preparing theme \"%s\"...", theme.c_str());
        ScreenPrint(-2, 3, "First start of this theme: its files are built from this");
        ScreenPrint(-2, 4, "console's Wii U Menu files. This can take a minute.");
        ScreenPrint(-2, 5, "Do not turn off the console.");
        if (files > 0) {
            ScreenPrint(-2, 7, "File %d of %d", file, files);
        }
        ScreenFlip();
    }

    void ShowPrepareFailed(const std::string &theme, const std::string &why, bool fallback) {
        ScreenClear();
        ScreenPrint(-2, -1, "PumpkinSpice " VERSION_FULL);
        ScreenPrint(-2, 1, "Theme \"%s\" could not be prepared:", theme.c_str());
        // the reason, wrapped at spaces to fit the GamePad
        int y            = 2;
        std::string rest = why.empty() ? std::string("unknown error") : why;
        while (!rest.empty() && y < 6) {
            size_t cut = rest.size();
            if (cut > 60) {
                cut = rest.rfind(' ', 60);
                cut = (cut == std::string::npos || cut == 0) ? 60 : cut;
            }
            ScreenPrint(-2, y++, "%s", rest.substr(0, cut).c_str());
            rest = rest.substr(cut < rest.size() && rest[cut] == ' ' ? cut + 1 : cut);
        }
        ScreenPrint(-2, y + 1, "The package stays on the SD card and is tried again");
        ScreenPrint(-2, y + 2, "the next time the Wii U Menu starts.");
        ScreenPrint(-2, y + 4, fallback ? "Starting with the files already in the theme folder." : "Starting the Wii U Menu without a theme.");
        ScreenPrint(-2, y + 6, "Press A to continue");
        ScreenFlip();

        InputInit();
        const OSTime start = OSGetTime();
        while (!(ReadButtonsTriggered() & VPAD_BUTTON_A) && OSTicksToMilliseconds(OSGetTime() - start) < 15000) {
            OSSleepTicks(OSMillisecondsToTicks(16));
        }
        InputDeInit();
    }
} // namespace

void ApplyTheme(const std::string &theme) {
    // a package (a .ps in the theme folder) becomes the regular theme folder on its first start (package.h); if that
    // fails, what is in the folder is used as it is
    std::string themePath     = std::string(THEMES_PATH) + "/" + theme;
    const std::string package = FindPackage(THEMES_PATH, theme);
    if (!package.empty()) {
        SDLOG("theme \"%s\" (package %s)", theme.c_str(), package.c_str());
        const bool screen = ScreenInit();
        OSTime lastDraw   = 0;
        if (screen) {
            DrawPreparing(theme, 0, 0);
        }
        const PackageProgress progress = [&](int file, int files) {
            // redrawn at most 10 times a second (a flip waits for the screen)
            if (file == files || OSTicksToMilliseconds(OSGetTime() - lastDraw) >= 100) {
                DrawPreparing(theme, file, files);
                lastDraw = OSGetTime();
            }
        };
        std::string why;
        const bool prepared = !PreparePackage(THEMES_PATH, theme, package, screen ? progress : nullptr, &why).empty();
        if (!prepared) {
            const bool fallback = IsDirectory(themePath);
            if (screen) {
                ShowPrepareFailed(theme, why, fallback);
            }
            ScreenDeInit();
            if (!fallback) {
                DEBUG_FUNCTION_LINE_ERR("Theme package \"%s\" could not be used", theme.c_str());
                return;
            }
            SDLOG("package \"%s\" not usable this time: the existing theme folder is loaded", theme.c_str());
        } else {
            ScreenDeInit();
        }
    } else {
        SDLOG("theme \"%s\"", theme.c_str());
    }
    DEBUG_FUNCTION_LINE("Applying theme \"%s\" from %s", theme.c_str(), themePath.c_str());
    AddContentLayer(themePath);
    if (gTitleFiles) {
        AddTitleLayers(themePath);
    } else {
        SDLOG("App icons and EULA off: titles folder not loaded");
    }
    SDLOG("layers added: %d", (int) gLayerHandles.size());
    ApplyCodePatches(themePath);
}

void RemoveTheme() {
    for (auto handle : gLayerHandles) {
        ContentRedirection_RemoveFSLayer(handle);
    }
    gLayerHandles.clear();
}
