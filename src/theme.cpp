#include "theme.h"
#include "codepatch.h"
#include "globals.h"
#include "utils/logger.h"
#include <algorithm>
#include <cctype>
#include <content_redirection/redirection.h>
#include <dirent.h>
#include <sys/stat.h>

namespace {

    bool IsDirectory(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
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

    // <theme>/titles/<16-digit title ID>/<folder> -> /vol/storage_mlc01/<sys|usr>/title/<high>/<low>/<folder>, merged.
    // The Menu reads other titles' files there itself, e.g. the EULA text (0005001b10050000/content) and every
    // app's icon (<title>/meta/iconTex.tga). Needs AddFSLayerEx (ContentRedirectionModule API version 2).
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
        for (const auto &id : ListDirs(titlesPath)) {
            if (id.size() != 16 || id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
                DEBUG_FUNCTION_LINE_WARN("Skipping %s/%s: not a 16-digit title ID", titlesPath.c_str(), id.c_str());
                continue;
            }
            std::string upper = id;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
            const std::string high = upper.substr(0, 8), low = upper.substr(8);
            // system titles (00050010 system apps, 0005001B system data, 00050030 applets) are in sys, others in usr
            const bool system      = high.rfind("0005001", 0) == 0 || high == "00050030";
            const std::string base = std::string("/vol/storage_mlc01/") + (system ? "sys" : "usr") + "/title/" + high + "/" + low + "/";
            for (const auto &folder : ListDirs(titlesPath + "/" + id)) {
                const std::string target      = base + folder;
                const std::string replacement = titlesPath + "/" + id + "/" + folder;
                const std::string name        = "PumpkinSpice " + upper + "/" + folder;
                CRLayerHandle handle          = 0;
                auto res                      = ContentRedirection_AddFSLayerEx(&handle, name.c_str(), target.c_str(), replacement.c_str(), FS_LAYER_TYPE_EX_MERGE_DIRECTORY);
                AddLayer(handle, res == CONTENT_REDIRECTION_RESULT_SUCCESS, target + " to " + replacement);
            }
        }
    }

} // namespace

std::vector<std::string> ListThemes() {
    return ListDirs(THEMES_PATH);
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
    AddContentLayer(themePath);
    AddTitleLayers(themePath);
    ApplyCodePatches(themePath);
}

void RemoveTheme() {
    for (auto handle : gLayerHandles) {
        ContentRedirection_RemoveFSLayer(handle);
    }
    gLayerHandles.clear();
}
