#include "theme.h"
#include "codepatch.h"
#include "globals.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include <algorithm>
#include <cctype>
#include <content_redirection/redirection.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

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

std::vector<std::string> ListThemes() {
    return ListDirs(THEMES_PATH);
}

namespace {
    int sStyleMiiUFound = -1; // -1 not checked yet this boot, 0 no, 1 found (and renamed)

    bool IsWps(const std::string &lowerName, const char *prefix) {
        const size_t n = strlen(prefix);
        return lowerName.size() >= n + 4 && lowerName.compare(0, n, prefix) == 0 &&
               lowerName.compare(lowerName.size() - 4, 4, ".wps") == 0;
    }
} // namespace

// StyleMiiU (the plugin Themiify installs themes for) themes the Wii U Menu too, and the two clash, so PumpkinSpice
// switches it off: in every environment's plugins folder that also holds PumpkinSpice's own .wps, stylemiiu*.wps is
// renamed to <name>1 (stylemiiu.wps1: Aroma only loads .wps files; names compared without case; if that name is taken,
// the next free <name>2 .. <name>9 is used, so no file is overwritten).
// Aroma loads plugins at boot, so StyleMiiU stays loaded until the next one: true means it was found (this boot).
bool SwitchOffStyleMiiU() {
    if (sStyleMiiUFound >= 0) {
        return sStyleMiiUFound == 1;
    }
    sStyleMiiUFound        = 0;
    const std::string envs = "fs:/vol/external01/wiiu/environments";
    auto lower             = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    };
    for (const auto &env : ListDirs(envs)) {
        const std::string plugins = envs + "/" + env + "/plugins";
        DIR *dir                  = opendir(plugins.c_str());
        if (!dir) {
            continue;
        }
        bool ours = false;
        std::vector<std::string> theirs;
        while (auto *entry = readdir(dir)) {
            const std::string name = lower(entry->d_name);
            if (IsWps(name, "stylemiiu")) {
                theirs.emplace_back(entry->d_name);
            } else if (IsWps(name, "pumpkinspice")) {
                ours = true;
            }
        }
        closedir(dir);
        if (!ours) {
            continue;
        }
        for (const auto &name : theirs) {
            const std::string from = plugins + "/" + name;
            std::string to         = from + "1";
            for (int i = 2; IsFile(to) && i < 10; i++) {
                to = from + std::to_string(i);
            }
            [[maybe_unused]] const bool ok = !IsFile(to) && rename(from.c_str(), to.c_str()) == 0; // only logged
            SDLOG("StyleMiiU found: %s -> %s %s", from.c_str(), to.c_str(), ok ? "renamed" : "NOT renamed");
            DEBUG_FUNCTION_LINE("StyleMiiU %s: %s", ok ? "switched off" : "could not be renamed", from.c_str());
            sStyleMiiUFound = 1;
        }
    }
    return sStyleMiiUFound == 1;
}

bool StyleMiiUSwitchedOffThisBoot() {
    return sStyleMiiUFound == 1;
}

bool IsWiiUMenu(uint64_t titleID) {
    return titleID == 0x0005001010040000ULL || titleID == 0x0005001010040100ULL || titleID == 0x0005001010040200ULL;
}

void ApplyTheme() {
    const auto themes = ListThemes();
    if (themes.empty()) {
        DEBUG_FUNCTION_LINE("No themes in %s", THEMES_PATH);
        return;
    }
    std::string theme = gActiveTheme;
    if (std::find(themes.begin(), themes.end(), theme) == themes.end()) {
        theme = themes.front();
    }
    const std::string themePath = std::string(THEMES_PATH) + "/" + theme;
    DEBUG_FUNCTION_LINE("Applying theme \"%s\"", theme.c_str());
    SDLOG("theme \"%s\"", theme.c_str());
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
