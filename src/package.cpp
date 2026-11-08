#include "package.h"
#include "utils/bigalloc.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include "wiiumenu/ps.hpp"
#include <cctype>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <new>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
    using wiiumenu::Bytes;
    using wiiumenu::FormatError;

    bool Exists(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0;
    }

    bool IsDir(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }


    bool ReadRange(FILE *f, uint64_t offset, uint64_t size, Bytes &out) {
        out.resize(size);
        return fseek(f, long(offset), SEEK_SET) == 0 && (size == 0 || fread(out.data(), 1, size, f) == size);
    }

    bool ReadAll(const std::string &path, Bytes &out) {
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        bool ok         = fseek(f, 0, SEEK_END) == 0;
        const long size = ok ? ftell(f) : -1;
        ok              = ok && size >= 0 && ReadRange(f, 0, uint64_t(size), out);
        fclose(f);
        return ok;
    }

    // creates the folders above a file path inside `root` (root itself exists)
    void MakeParents(const std::string &root, const std::string &path) {
        for (size_t i = root.size() + 1; (i = path.find('/', i)) != std::string::npos; i++) {
            mkdir(path.substr(0, i).c_str(), 0777);
        }
    }

    bool WriteAll(const std::string &root, const std::string &path, const uint8_t *data, size_t size) {
        MakeParents(root, path);
        FILE *f = fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        const bool ok = size == 0 || fwrite(data, 1, size, f) == size;
        return (fclose(f) == 0) && ok;
    }

    // copies part of the package to a file in pieces (added files can be large, e.g. music)
    bool CopyRange(FILE *in, uint64_t offset, uint64_t size, const std::string &root, const std::string &path) {
        MakeParents(root, path);
        FILE *out = fopen(path.c_str(), "wb");
        if (!out) {
            return false;
        }
        bool ok = fseek(in, long(offset), SEEK_SET) == 0;
        static uint8_t buf[0x10000];
        while (ok && size > 0) {
            const size_t n = size < sizeof(buf) ? size_t(size) : sizeof(buf);
            ok             = fread(buf, 1, n, in) == n && fwrite(buf, 1, n, out) == n;
            size -= n;
        }
        return (fclose(out) == 0) && ok;
    }

    // removes a folder PumpkinSpice made itself (<name>.building)
    void RemoveTree(const std::string &dir) {
        DIR *d = opendir(dir.c_str());
        if (!d) {
            return;
        }
        while (auto *e = readdir(d)) {
            const std::string name = e->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            const std::string p = dir + "/" + name;
            if (IsDir(p)) {
                RemoveTree(p);
            } else {
                unlink(p.c_str());
            }
        }
        closedir(d);
        rmdir(dir.c_str());
    }

    // The console's region, from the running Wii U Menu's title ID (00050010-10040000 JPN, -10040100 USA, -10040200 EUR)
    std::string ConsoleRegion() {
        switch (OSGetTitleID()) {
            case 0x0005001010040000ULL:
                return "JPN";
            case 0x0005001010040100ULL:
                return "USA";
            case 0x0005001010040200ULL:
                return "EUR";
            default:
                return "";
        }
    }
} // namespace

void RecoverInterruptedPackages(const std::string &themesDir) {
    DIR *d = opendir(themesDir.c_str());
    if (!d) {
        return;
    }
    std::vector<std::string> building, replaced;
    while (auto *e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > 9 && n.compare(n.size() - 9, 9, ".building") == 0) {
            building.push_back(n.substr(0, n.size() - 9));
        } else if (n.size() > 9 && n.compare(n.size() - 9, 9, ".replaced") == 0) {
            replaced.push_back(n.substr(0, n.size() - 9));
        }
    }
    closedir(d);
    for (const auto &name : building) {
        const std::string folder = themesDir + "/" + name;
        const std::string old    = folder + ".replaced";
        if (Exists(folder)) {
            continue; // an interrupted rebuild: PreparePackage starts it again
        }
        // the folder was moved aside and the new one not yet put in place: it goes back, with its package
        if (Exists(old) && rename(old.c_str(), folder.c_str()) == 0) {
            RemoveTree(folder + ".building");
            SDLOG("package \"%s\": interrupted while being put in place; the theme folder is restored and rebuilt",
                  name.c_str());
        }
    }
    // the new folder was put in place but the old one not yet deleted: delete it now
    for (const auto &name : replaced) {
        const std::string folder = themesDir + "/" + name;
        if (IsDir(folder) && !Exists(folder + ".building")) {
            RemoveTree(folder + ".replaced");
            SDLOG("package \"%s\": the old theme left by an interrupted update is deleted", name.c_str());
        }
    }
}

std::string FindPackage(const std::string &themesDir, const std::string &name) {
    const std::string folder = themesDir + "/" + name;
    std::string found;
    if (DIR *d = opendir(folder.c_str())) { // the package inside the theme folder
        while (auto *e = readdir(d)) {
            std::string n = e->d_name;
            for (auto &c : n) {
                c = char(tolower(static_cast<unsigned char>(c)));
            }
            const std::string p = folder + "/" + e->d_name;
            if (n.size() > 3 && n.compare(n.size() - 3, 3, ".ps") == 0 && !IsDir(p) && (found.empty() || p < found)) {
                found = p;
            }
        }
        closedir(d);
    }
    if (found.empty() && Exists(folder + ".ps") && !IsDir(folder + ".ps")) { // or next to it
        found = folder + ".ps";
    }
    return found;
}

std::string PreparePackage(const std::string &themesDir, const std::string &name, const std::string &packagePath,
                           const PackageProgress &progress, std::string *error) {
    // the first reason it failed, for the screen
    auto reason = [&](const std::string &why) {
        if (error && error->empty()) {
            *error = why;
        }
    };
    const std::string folder = themesDir + "/" + name;
    const std::string temp   = folder + ".building";
    const bool inside        = packagePath.rfind(folder + "/", 0) == 0;
    FILE *pf                 = fopen(packagePath.c_str(), "rb");
    if (!pf) {
        SDLOG("package %s: cannot open", packagePath.c_str());
        reason("the package file cannot be opened");
        return "";
    }
    bool ok = false;
    AppHeapFallback appHeap; // big blocks the mapped memory can't give: from the Menu's heap (all freed before it runs)
    SDLOG("package: largest free block in the Wii U Menu's heap %u KB", unsigned(AppHeapLargestFree() / 1024));
    TakeLastFailedAllocation();
    try {
        Bytes head;
        if (!ReadRange(pf, 0, wiiumenu::ps::kHeaderSize, head)) {
            throw FormatError("cannot read the header");
        }
        const uint32_t manifestSize = wiiumenu::ps::manifest_size(head);
        if (!ReadRange(pf, 0, wiiumenu::ps::kHeaderSize + uint64_t(manifestSize), head)) {
            throw FormatError("cannot read the manifest");
        }
        const wiiumenu::ps::Manifest m = wiiumenu::ps::read_manifest(head);
        const std::string region       = ConsoleRegion();
        if (region.empty()) {
            throw FormatError("not running in the Wii U Menu: region unknown");
        }

        SDLOG("package \"%s\": rebuilding the theme from this console's files (%s)", name.c_str(), region.c_str());
        DEBUG_FUNCTION_LINE("Rebuilding theme package \"%s\"", name.c_str());
        [[maybe_unused]] const uint32_t t0 = OSGetTick(); // only logged
        RemoveTree(temp);                                 // left over from an interrupted rebuild
        mkdir(temp.c_str(), 0777);
        const wiiumenu::ps::BlobReader reader = [&](uint64_t offset, uint64_t size) {
            Bytes b;
            if (!ReadRange(pf, m.data_start + offset, size, b)) {
                throw FormatError("cannot read the package");
            }
            return b;
        };
        int written = 0, skipped = 0, failed = 0, otherRegion = 0;
        std::set<std::string> done;                 // files written (a file can have one node per stock version)
        std::map<std::string, std::string> notDone; // file -> why its nodes were skipped so far
        std::set<std::string> regionFiles, started; // for progress
        for (const wiiumenu::json::Value &node : m.doc.at("files").array()) {
            if (wiiumenu::ps::for_region(node, region)) {
                regionFiles.insert(node.at("path").string());
            }
        }
        for (const wiiumenu::json::Value &node : m.doc.at("files").array()) {
            const std::string &rel   = node.at("path").string();
            const std::string target = temp + "/" + rel;
            if (!wiiumenu::ps::for_region(node, region)) {
                otherRegion++;
                continue;
            }
            if (done.count(rel)) {
                continue;
            }
            if (started.insert(rel).second && progress) {
                progress(int(started.size()), int(regionFiles.size()));
            }
            try {
                if (node.at("op").string() == "add") { // the theme's own file: copied in pieces
                    const auto [off, size] = wiiumenu::ps::blob_range(node);
                    if (CopyRange(pf, m.data_start + off, size, temp, target)) {
                        written++;
                        done.insert(rel);
                    } else {
                        SDLOG("  %s: could not be written", rel.c_str());
                        reason(rel + " could not be written to the SD card");
                        failed++;
                    }
                    continue;
                }
                const std::string stockPath = wiiumenu::ps::console_path(rel);
                Bytes stock;
                const bool haveStock = !stockPath.empty() && ReadAll("fs:" + stockPath, stock);
                std::string why;
                const std::optional<Bytes> out = wiiumenu::ps::rebuild(node, reader, haveStock ? &stock : nullptr, &why);
                if (!out) {
                    notDone[rel] = why; // another node (stock version) of this file may still match
                } else if (WriteAll(temp, target, out->data(), out->size())) {
                    written++;
                    done.insert(rel);
                    notDone.erase(rel);
                } else {
                    SDLOG("  %s: could not be written", rel.c_str());
                    reason(rel + " could not be written to the SD card");
                    failed++;
                }
            } catch (const std::bad_alloc &) {
                SDLOG("  %s: not enough memory to rebuild it (a block of %u KB; largest free in the Menu's heap %u KB)",
                      rel.c_str(), unsigned(TakeLastFailedAllocation() / 1024), unsigned(AppHeapLargestFree() / 1024));
                reason("not enough memory to rebuild " + rel);
                failed++;
            } catch (const std::exception &e) {
                SDLOG("  %s: %s", rel.c_str(), e.what());
                reason(rel + ": " + e.what());
                failed++;
            }
        }
        for ([[maybe_unused]] const auto &entry : notDone) { // the file name and why: only logged
            SDLOG("  %s: skipped (%s)", entry.first.c_str(), entry.second.c_str());
            skipped++;
        }
        if (const wiiumenu::json::Value *code = m.doc.find("code")) {
            const std::string &c = code->string();
            WriteAll(temp, temp + "/code.txt", reinterpret_cast<const uint8_t *>(c.data()), c.size());
        }
        SDLOG("package \"%s\": %d files rebuilt, %d for other regions, %d skipped (no matching stock file), %d failed, %u ms",
              name.c_str(), written, otherRegion, skipped, failed, (unsigned) OSTicksToMilliseconds(OSGetTick() - t0));
        ok = failed == 0;
    } catch (const std::bad_alloc &) {
        SDLOG("package %s: not enough memory", packagePath.c_str());
        reason("not enough memory");
    } catch (const std::exception &e) {
        SDLOG("package %s: %s", packagePath.c_str(), e.what());
        reason(e.what());
        DEBUG_FUNCTION_LINE_ERR("Theme package %s: %s", packagePath.c_str(), e.what());
    }
    fclose(pf);
    if (!ok) {
        RemoveTree(temp); // the package stays: it is tried again on the next boot
        DEBUG_FUNCTION_LINE_ERR("Theme package \"%s\" could not be rebuilt", name.c_str());
        return "";
    }

    // the finished files become the theme folder. The folder that was there (the package, and an earlier version's
    // files if any) is moved aside as <name>.replaced first, so a power cut never leaves the theme without a folder
    // (RecoverInterruptedPackages), and then deleted with the package in it.
    std::string old;
    if (IsDir(folder)) {
        old = folder + ".replaced";
        RemoveTree(old); // left over from a power cut after an earlier swap
        if (rename(folder.c_str(), old.c_str()) != 0) {
            SDLOG("package \"%s\": the existing theme folder could not be moved aside", name.c_str());
            reason("the existing theme folder could not be renamed");
            RemoveTree(temp);
            return "";
        }
    }
    if (rename(temp.c_str(), folder.c_str()) != 0) {
        SDLOG("package \"%s\": the theme folder could not be put in place", name.c_str());
        reason("the rebuilt theme folder could not be put in place");
        if (!old.empty()) {
            rename(old.c_str(), folder.c_str()); // put it back
        }
        RemoveTree(temp);
        return "";
    }
    if (!old.empty()) {
        RemoveTree(old); // the old theme, with the package
    }
    if (!inside) {
        unlink(packagePath.c_str()); // a package next to the folder
    }
    [[maybe_unused]] const bool deleted = !Exists(packagePath) && (old.empty() || !Exists(old)); // only logged
    SDLOG("package \"%s\": installed as the theme folder %s%s", name.c_str(), folder.c_str(),
          deleted ? "; the old theme and the package are deleted" : "; the old theme or the package could not be deleted");
    return folder;
}
