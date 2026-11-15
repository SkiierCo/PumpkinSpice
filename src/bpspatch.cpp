#include "bpspatch.h"
#include "utils/bigalloc.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include "wiiumenu/bps.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
    using wiiumenu::Bytes;

    std::string Lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }

    bool IsDir(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool ReadAll(const std::string &path, Bytes &out) {
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        bool ok         = fseek(f, 0, SEEK_END) == 0;
        const long size = ok ? ftell(f) : -1;
        ok              = ok && size >= 0 && fseek(f, 0, SEEK_SET) == 0;
        if (ok) {
            out.resize(size_t(size));
            ok = size == 0 || fread(out.data(), 1, size_t(size), f) == size_t(size);
        }
        fclose(f);
        return ok;
    }

    // written as <path>.part first and then renamed, so a console turned off halfway leaves the old file (or none)
    bool WriteAll(const std::string &root, const std::string &path, const Bytes &data) {
        for (size_t i = root.size() + 1; (i = path.find('/', i)) != std::string::npos; i++) {
            mkdir(path.substr(0, i).c_str(), 0777);
        }
        const std::string part = path + ".part";
        FILE *f                = fopen(part.c_str(), "wb");
        if (!f) {
            return false;
        }
        const bool ok = (data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size()) && fclose(f) == 0;
        if (!ok) {
            unlink(part.c_str());
            return false;
        }
        unlink(path.c_str());
        return rename(part.c_str(), path.c_str()) == 0;
    }

    struct Patch {
        std::string bps;  // the .bps file
        std::string rel;  // the file it makes, "content/..."
        std::string name; // for messages: the .bps path inside the theme folder
    };

    // <file>.bps anywhere under content/
    void FindInContent(const std::string &themePath, const std::string &dir, std::vector<Patch> &out) {
        DIR *d = opendir((themePath + "/" + dir).c_str());
        if (!d) {
            return;
        }
        std::vector<std::string> names;
        while (auto *e = readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != "..") {
                names.push_back(n);
            }
        }
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto &n : names) {
            const std::string rel = dir + "/" + n;
            if (IsDir(themePath + "/" + rel)) {
                FindInContent(themePath, rel, out);
            } else if (n.size() > 4 && Lower(n.substr(n.size() - 4)) == ".bps") {
                out.push_back({themePath + "/" + rel, rel.substr(0, rel.size() - 4), rel});
            }
        }
    }

    std::vector<Patch> FindPatches(const std::string &themePath) {
        std::vector<Patch> out;
        FindInContent(themePath, "content", out);
        // Men.bps, Men2.bps, cafe_barista_men.bps loose in the theme folder
        const std::pair<const char *, const char *> loose[] = {
                {"men.bps", "content/Common/Package/Men.pack"},
                {"men2.bps", "content/Common/Package/Men2.pack"},
                {"cafe_barista_men.bps", "content/Common/Sound/Men/cafe_barista_men.bfsar"},
        };
        if (DIR *d = opendir(themePath.c_str())) {
            while (auto *e = readdir(d)) {
                const std::string n = e->d_name;
                for (const auto &[file, rel] : loose) {
                    if (Lower(n) != file || IsDir(themePath + "/" + n)) {
                        continue;
                    }
                    // the same file from a content/ patch too: that one wins, this one stays and is logged
                    if (std::any_of(out.begin(), out.end(), [&](const Patch &p) { return p.rel == rel; })) {
                        SDLOG("bps: %s skipped, content/ has a patch for %s too", n.c_str(), rel);
                        continue;
                    }
                    out.push_back({themePath + "/" + n, rel, n});
                }
            }
            closedir(d);
        }
        return out;
    }
} // namespace

bool HasBpsPatches(const std::string &themePath) {
    return !FindPatches(themePath).empty();
}

bool ApplyBpsPatches(const std::string &themePath, const PackageProgress &progress, std::string *error) {
    const std::vector<Patch> patches = FindPatches(themePath);
    AppHeapFallback appHeap; // the Menu's files are up to about 10 MB, held with the patch and the result
    std::string reasons;
    int n = 0, applied = 0;
    for (const Patch &p : patches) {
        if (progress) {
            progress(++n, int(patches.size()));
        }
        std::string why;
        try {
            Bytes patch, source;
            if (!ReadAll(p.bps, patch)) {
                why = "couldn't be read";
            } else if (!ReadAll("fs:/vol/" + p.rel, source)) {
                why = "this console has no " + p.rel.substr(8);
            } else {
                const Bytes target = wiiumenu::bps::apply(patch, source);
                if (!WriteAll(themePath, themePath + "/" + p.rel, target)) {
                    why = "couldn't write " + p.rel + " to the SD card";
                } else {
                    unlink(p.bps.c_str());
                    applied++;
                    SDLOG("bps: %s applied -> %s (%u bytes)", p.name.c_str(), p.rel.c_str(), (unsigned) target.size());
                }
            }
        } catch (const std::exception &e) {
            why = e.what();
        }
        if (!why.empty()) {
            SDLOG("bps: %s: %s", p.name.c_str(), why.c_str());
            reasons += (reasons.empty() ? "" : "; ") + p.name + ": " + why;
        }
    }
    if (error) {
        *error = reasons;
    }
    return applied == int(patches.size());
}
