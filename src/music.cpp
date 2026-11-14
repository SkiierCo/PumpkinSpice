// Shuffle music (v1.5). A theme's sound effects and its song both live in its cafe_barista_men.bfsar: in the themes
// made the regular way the song is wave 302 of wave archive 1, played by two short one-note sequences (BGM_MEN_TV and
// BGM_MEN_DRC_MAIN), and in a streamed theme like Persona Advanced RELOADED it's stream/BGM_MEN.bfstm next to the
// archive. So the song can't be swapped as a file on its own. Instead, once (like a theme package), everything gets
// ready in sd:/wiiu/pumpkinspice/music-cache from the themes and the music folder:
//   songs/<id>.bfstm            every regular song's wave 302 as a stream: its DSP-ADPCM data as it is, nothing encoded
//                               again (bfstm::write_adpcm). A streamed song's BGM_MEN.bfstm is used where it is.
//   menus/<theme> (<tv> <gamepad>).bfsar
//                               every theme's archive playing its music from stream/BGM_MEN.bfstm at that volume, with
//                               its own sound effects (a regular theme through bfsar::stream_music, LatteLab menu-music's
//                               steps; a streamed one only gets the volume). One per volume a song uses, because the
//                               volume is the archive's, not the song's. "(default)" is the console's own archive, for
//                               themes without one, so those play the default sound effects.
//   records/                    what each file was made from (size and time of the archive), so only what changed is
//                               done again
//   default.bfsar               the console's own archive, for the easter egg (default music and sound effects)
//   BGM_SET_ACS.bfstm           the console's own stream, the layout every song stream copies
// The same song is often in several places (the owner's Persona Advanced DX, ReZERO and Wii U Modern themes and the
// music folder's A Way of Life are byte for byte the same audio, 2026-11-14), so a song is known by its audio, its id
// a hash of it, and so is one with the same folder name and length (within a second; it catches a song encoded again;
// the owner asked for both, 2026-11-14): one stream file, one entry in the shuffle and its history. A music folder copy of a song a theme or an
// earlier music folder song already has is deleted (owner's call, 2026-11-14; themes are never touched), its
// music-volume.txt moved to the copy that stays if that one has none. Cache files nothing uses any more go too.
// A song's volume is TV 44, GamePad 41 (the owner's default for streamed music, 2026-11-13) unless a music-volume.txt
// next to one of its copies' archive (a theme: in the theme's folder) says otherwise. Songs made from the original
// soundtrack files are louder: the owner's Persona Advanced DX and RELOADED songs use 24 22 (2026-11-14).
// Every Menu start then only adds two single-file layers over the theme: the song as stream/BGM_MEN.bfstm and the
// theme's menus/ archive for that song's volume. ContentRedirection checks the newest layer first, so they win over the
// theme's own files. A streamed archive without its stream file crashes the Menu (Cemu, 2026-11-13), so the song layer
// goes in first and the archive layer only if it took.
#include "music.h"
#include "bootscreen.h"
#include "globals.h"
#include "themeSelector.h"
#include "utils/bigalloc.h"
#include "utils/logger.h"
#include "utils/sdlog.h"
#include "wiiumenu/bfsar.hpp"
#include "wiiumenu/bfstm.hpp"
#include <algorithm>
#include <content_redirection/redirection.h>
#include <coreinit/time.h>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <map>
#include <notifications/notifications.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <wups/storage.h>

namespace {
    using wiiumenu::Bytes;
    using wiiumenu::FormatError;
    namespace bfsar = wiiumenu::bfsar;
    namespace bfstm = wiiumenu::bfstm;

    const std::string kSoundDir         = "/content/Common/Sound/Men";
    const std::string kArchive          = "cafe_barista_men.bfsar";
    const std::string kVolumeFile       = "music-volume.txt";
    const std::string kDefault          = "(default)"; // menus/ name of the console's own archive
    const char *kConsoleArchive         = "fs:/vol/content/Common/Sound/Men/cafe_barista_men.bfsar";
    const char *kConsoleTemplate        = "fs:/vol/content/Common/Sound/Men/stream/BGM_SET_ACS.bfstm";
    constexpr uint8_t kTvVolume         = 44;
    constexpr uint8_t kDrcVolume        = 41;
    constexpr uint32_t kEasterEggChance = 100; // 1 in this many Menu starts

    struct Volume {
        uint8_t tv = kTvVolume, drc = kDrcVolume;
        bool operator<(const Volume &o) const { return tv != o.tv ? tv < o.tv : drc < o.drc; }
        std::string Name() const { return std::to_string(tv) + " " + std::to_string(drc); }
    };

    std::string Cache(const std::string &rel) {
        return std::string(MUSIC_CACHE_PATH) + "/" + rel;
    }

    bool IsDir(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool IsFile(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
    }

    // size and time of a file, "" if it isn't there
    std::string Stamp(const std::string &path) {
        struct stat st {};
        if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
            return "";
        }
        return std::to_string((long long) st.st_size) + " " + std::to_string((long long) st.st_mtime);
    }

    // the file names in a folder (not folders), sorted
    std::vector<std::string> Files(const std::string &dir) {
        std::vector<std::string> out;
        if (DIR *d = opendir(dir.c_str())) {
            while (auto *e = readdir(d)) {
                const std::string n = e->d_name;
                if (n != "." && n != ".." && IsFile(dir + "/" + n)) {
                    out.push_back(n);
                }
            }
            closedir(d);
        }
        std::sort(out.begin(), out.end());
        return out;
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

    // written as <path>.part first and then renamed, so a console turned off halfway leaves no half file behind
    bool WriteAll(const std::string &path, const uint8_t *data, size_t size) {
        mkdir(MUSIC_CACHE_PATH, 0777);
        for (size_t i = std::string(MUSIC_CACHE_PATH).size() + 1; (i = path.find('/', i)) != std::string::npos; i++) {
            mkdir(path.substr(0, i).c_str(), 0777);
        }
        const std::string part = path + ".part";
        FILE *f                = fopen(part.c_str(), "wb");
        if (!f) {
            return false;
        }
        const bool ok = (size == 0 || fwrite(data, 1, size, f) == size) && fclose(f) == 0;
        if (!ok) {
            unlink(part.c_str());
            return false;
        }
        unlink(path.c_str());
        return rename(part.c_str(), path.c_str()) == 0;
    }

    bool WriteText(const std::string &path, const std::string &text) {
        return WriteAll(path, reinterpret_cast<const uint8_t *>(text.data()), text.size());
    }

    std::string ReadText(const std::string &path) {
        Bytes b;
        return ReadAll(path, b) ? std::string(b.begin(), b.end()) : std::string();
    }

    // the lines of a record, "" for each one that's missing
    std::vector<std::string> ReadRecord(const std::string &name, size_t lines) {
        const std::string t = ReadText(Cache("records/" + name + ".txt"));
        std::vector<std::string> out;
        size_t at = 0;
        while (out.size() < lines) {
            const size_t end = t.find('\n', at);
            if (end == std::string::npos) {
                break;
            }
            out.push_back(t.substr(at, end - at));
            at = end + 1;
        }
        out.resize(lines);
        return out;
    }

    void WriteRecord(const std::string &name, const std::vector<std::string> &lines) {
        std::string t;
        for (const auto &l : lines) {
            t += l + "\n";
        }
        WriteText(Cache("records/" + name + ".txt"), t);
    }

    // 64-bit FNV-1a, for telling songs apart by their audio
    struct Hash {
        uint64_t h = 1469598103934665603ULL;
        void Add(const uint8_t *p, size_t n) {
            for (size_t i = 0; i < n; i++) {
                h = (h ^ p[i]) * 1099511628211ULL;
            }
        }
        void Add(uint64_t v) {
            for (int i = 0; i < 8; i++) {
                const uint8_t b = uint8_t(v >> (8 * i));
                Add(&b, 1);
            }
        }
        std::string Hex(char kind) const {
            char buf[24];
            snprintf(buf, sizeof(buf), "%c%016llx", kind, (unsigned long long) h);
            return buf;
        }
    };

    // music-volume.txt: the first line that isn't empty or a # comment, "<tv> <gamepad>", each 0 to 127 or a percentage
    // of 127 ("19%"). Returns false if there's no file or nothing readable in it.
    bool ReadVolume(const std::string &path, Volume &v) {
        const std::string t = ReadText(path);
        size_t at           = 0;
        while (at < t.size()) {
            size_t end = t.find('\n', at);
            if (end == std::string::npos) {
                end = t.size();
            }
            std::string line  = t.substr(at, end - at);
            at                = end + 1;
            const size_t hash = line.find('#');
            if (hash != std::string::npos) {
                line.erase(hash);
            }
            int n[2] = {-1, -1}, got = 0;
            const char *p = line.c_str();
            while (*p && got < 2) {
                char *e      = nullptr;
                const long x = std::strtol(p, &e, 10);
                if (e == p) {
                    p++;
                    continue;
                }
                n[got++] = *e == '%' ? int((x * 127 + 50) / 100) : int(x);
                p        = *e == '%' ? e + 1 : e;
            }
            if (got == 2) {
                v.tv  = uint8_t(std::clamp(n[0], 0, 127));
                v.drc = uint8_t(std::clamp(n[1], 0, 127));
                return true;
            }
        }
        return false;
    }

    // One place a song can come from: a theme, or a folder in the music folder.
    struct Source {
        std::string name;    // "theme - <theme>" or "track - <folder>" (folders under music joined with " - ")
        std::string theme;   // the theme, "" for a music folder song
        std::string folder;  // the folder holding cafe_barista_men.bfsar (and stream/BGM_MEN.bfstm for a streamed one)
        std::string archive; // folder + "/" + kArchive
        std::string volume;  // its music-volume.txt
        std::string label;   // folder name for spotting the same song: the theme, or the music folder song's own folder
    };

    std::string Lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }

    // a stream file's length in milliseconds (its INFO block: sample rate, sample count), 0 if it can't be read
    uint32_t StreamLength(const Bytes &b) {
        try {
            if (b.size() < 0x40 || std::string(b.begin(), b.begin() + 4) != "FSTM") {
                return 0;
            }
            for (uint16_t i = 0, n = wiiumenu::read_u16(b, 0x10, true); i < n; i++) {
                if (wiiumenu::read_u16(b, 0x14 + 12 * i, true) != 0x4000) {
                    continue;
                }
                const size_t body   = wiiumenu::read_u32(b, 0x18 + 12 * i, true) + 8;
                const size_t si     = body + wiiumenu::read_u32(b, body + 4, true);
                const uint32_t rate = wiiumenu::read_u32(b, si + 4, true), samples = wiiumenu::read_u32(b, si + 0x0C, true);
                return rate ? uint32_t(uint64_t(samples) * 1000 / rate) : 0;
            }
        } catch (const std::exception &) {
        }
        return 0;
    }

    // every music folder song: a folder (up to 3 levels down) with a cafe_barista_men.bfsar in it
    void FindTracks(const std::string &dir, const std::string &prefix, int depth, std::vector<Source> &out) {
        DIR *d = opendir(dir.c_str());
        if (!d) {
            return;
        }
        std::vector<std::string> names;
        while (auto *e = readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != ".." && IsDir(dir + "/" + n)) {
                names.push_back(n);
            }
        }
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const auto &n : names) {
            const std::string folder = dir + "/" + n, label = prefix.empty() ? n : prefix + " - " + n;
            if (IsFile(folder + "/" + kArchive)) {
                out.push_back({"track - " + label, "", folder, folder + "/" + kArchive, folder + "/" + kVolumeFile, n});
            } else if (depth < 3) {
                FindTracks(folder, label, depth + 1, out);
            }
        }
    }

    // the themes' songs first, then the music folder's, so a theme's copy is always the one kept
    std::vector<Source> Sources(const std::vector<std::string> &themes) {
        std::vector<Source> out;
        for (const auto &t : themes) {
            const std::string root = std::string(THEMES_PATH) + "/" + t, folder = root + kSoundDir;
            if (IsFile(folder + "/" + kArchive)) {
                out.push_back({"theme - " + t, t, folder, folder + "/" + kArchive, root + "/" + kVolumeFile, t});
            }
        }
        FindTracks(MUSIC_PATH, "", 1, out);
        return out;
    }

    // where a theme's menus come from: its own archive, or "" for the console's own (themes without one)
    std::string MenuSource(const std::string &theme) {
        const std::string a = std::string(THEMES_PATH) + "/" + theme + kSoundDir + "/" + kArchive;
        return IsFile(a) ? a : "";
    }

    std::string MenuName(const std::string &theme, const Volume &v) {
        return (MenuSource(theme).empty() ? kDefault : theme) + " (" + v.Name() + ")";
    }

    void DrawPreparing(int item, int items) {
        ScreenClear();
        ScreenPrint(-2, -1, "PumpkinSpice " VERSION_FULL);
        ScreenPrint(-2, 1, "Preparing music...");
        ScreenPrint(-2, 3, "First start with music shuffle: songs are copied out of the themes");
        ScreenPrint(-2, 4, "and the music folder. This can take a few minutes.");
        ScreenPrint(-2, 5, "Do not turn off the console.");
        if (items > 0) {
            ScreenPrint(-2, 7, "File %d of %d", item, items);
        }
        ScreenFlip();
    }

    // What a source's song is: its kind ("wave": stream written to songs/<id>.bfstm; "stream": the archive's own stream
    // file; "none"), its id (a hash of the audio, the same for the same song wherever it is) and its length in ms
    struct SongInfo {
        std::string kind = "none", id;
        uint32_t ms      = 0;
    };

    SongInfo MakeSong(const Source &s, const Bytes &sar, const Bytes &tpl) {
        const bfsar::Archive a = bfsar::read(sar);
        const auto tv          = std::find_if(a.sounds.begin(), a.sounds.end(), [](const bfsar::Sound &x) { return x.name == "BGM_MEN_TV"; });
        if (tv == a.sounds.end()) {
            return {};
        }
        if (tv->type == bfsar::SoundType::Stream) {
            const std::string file = s.folder + "/stream/BGM_MEN.bfstm";
            const bool own         = tv->file < a.files.size() && a.files[tv->file].external && a.files[tv->file].path == "stream/BGM_MEN.bfstm";
            Bytes stream;
            if (!own || !ReadAll(file, stream)) {
                return {};
            }
            Hash h;
            h.Add(stream.data(), stream.size());
            return {"stream", h.Hex('s'), StreamLength(stream)};
        }
        // the regular way: BGM_MEN_TV a short sequence (416 bytes in these themes, 5664 in the console's own music)
        // and BANK_MEN_BGM playing wave 302
        if (tv->type != bfsar::SoundType::Sequence || tv->file >= a.files.size() || a.files[tv->file].size > 1024) {
            return {};
        }
        const auto bank = std::find_if(a.banks.begin(), a.banks.end(), [](const bfsar::Named &b) { return b.name == "BANK_MEN_BGM"; });
        if (bank == a.banks.end()) {
            return {};
        }
        const auto waves = bfsar::bank_waves(sar, bank->index, 1);
        if (std::find(waves.begin(), waves.end(), 302u) == waves.end()) {
            return {};
        }
        const bfsar::Wave w = bfsar::read_wave(sar, 1, 302);
        if (w.encoding != 2 || w.adpcm.empty() || w.samples < 14 * 1024 || !w.sample_rate) {
            SDLOG("music: %s: wave 302 isn't a DSP-ADPCM song (encoding %u, %u samples)", s.name.c_str(), w.encoding, (unsigned) w.samples);
            return {};
        }
        Hash h;
        h.Add(w.sample_rate);
        h.Add(w.samples);
        for (const auto &c : w.adpcm) {
            h.Add(reinterpret_cast<const uint8_t *>(c.coefs), sizeof(c.coefs));
            h.Add(c.data.data(), c.data.size());
        }
        const SongInfo info{"wave", h.Hex('w'), uint32_t(uint64_t(w.samples) * 1000 / w.sample_rate)};
        const std::string id = info.id, file = Cache("songs/" + id + ".bfstm");
        if (IsFile(file)) {
            SDLOG("music: %s: same song as songs/%s.bfstm", s.name.c_str(), id.c_str());
            return info;
        }
        bfstm::Options opt;
        opt.loop_start     = 0;
        opt.loop           = true;
        const Bytes stream = bfstm::write_adpcm(tpl, w.adpcm, w.samples, w.sample_rate, opt);
        if (!WriteAll(file, stream.data(), stream.size())) {
            throw FormatError("couldn't write the song to the SD card");
        }
        SDLOG("music: %s: song %u ch, %u Hz, %u samples, stream %u bytes", s.name.c_str(), (unsigned) w.adpcm.size(),
              (unsigned) w.sample_rate, (unsigned) w.samples, (unsigned) stream.size());
        return info;
    }

    // A theme's (or the console's) archive playing its music from stream/BGM_MEN.bfstm at volume `v`
    void MakeMenu(const std::string &name, const Bytes &sar, const Volume &v) {
        const bfsar::Archive a = bfsar::read(sar);
        const auto tv          = std::find_if(a.sounds.begin(), a.sounds.end(), [](const bfsar::Sound &x) { return x.name == "BGM_MEN_TV"; });
        const bool streamed    = tv != a.sounds.end() && tv->type == bfsar::SoundType::Stream && tv->file < a.files.size() &&
                              a.files[tv->file].external && a.files[tv->file].path == "stream/BGM_MEN.bfstm";
        Bytes out;
        if (streamed) {
            out = bfsar::set_sound_volume(sar, "BGM_MEN_TV", v.tv);
            out = bfsar::set_sound_volume(out, "BGM_MEN_DRC_MAIN", v.drc);
        } else {
            out = bfsar::stream_music(sar, v.tv, v.drc);
        }
        if (!WriteAll(Cache("menus/" + name + ".bfsar"), out.data(), out.size())) {
            throw FormatError("couldn't write the sound archive to the SD card");
        }
    }

    // One song, wherever it is: the copies that have it (themes first), its stream file (the first copy's) and volume.
    struct Song {
        std::string name; // the first copy's, for the shuffle and its history
        std::vector<const Source *> copies;
        std::vector<std::string> ids; // every copy's audio id
        std::vector<uint32_t> ms;     // every copy's length
        std::string stream;
        Volume volume;
    };

    // The songs that are ready, from the records (each source's record: stamp, kind, id, length in ms). Copies with the
    // same audio, or the same folder name and a length within a second, are one song.
    std::vector<Song> Songs(const std::vector<Source> &sources) {
        std::vector<Song> out;
        for (const auto &src : sources) {
            const auto r = ReadRecord(src.name, 4);
            if (r[0] != Stamp(src.archive) || (r[1] != "wave" && r[1] != "stream") || r[2].empty()) {
                continue;
            }
            const std::string stream = r[1] == "wave" ? Cache("songs/" + r[2] + ".bfstm") : src.folder + "/stream/BGM_MEN.bfstm";
            if (!IsFile(stream)) {
                continue;
            }
            const uint32_t ms = uint32_t(std::strtoul(r[3].c_str(), nullptr, 10));
            auto it           = std::find_if(out.begin(), out.end(), [&](const Song &s) {
                for (size_t i = 0; i < s.copies.size(); i++) {
                    if (s.ids[i] == r[2]) {
                        return true;
                    }
                    const uint32_t d = s.ms[i] > ms ? s.ms[i] - ms : ms - s.ms[i];
                    if (ms && s.ms[i] && d <= 1000 && Lower(s.copies[i]->label) == Lower(src.label)) {
                        return true;
                    }
                }
                return false;
                      });
            if (it == out.end()) {
                Song s;
                s.name   = src.name;
                s.stream = stream;
                out.push_back(s);
                it = out.end() - 1;
            }
            it->copies.push_back(&src);
            it->ids.push_back(r[2]);
            it->ms.push_back(ms);
        }
        for (auto &s : out) {
            for (const Source *c : s.copies) {
                if (ReadVolume(c->volume, s.volume)) {
                    break; // the first copy with a music-volume.txt sets it
                }
            }
        }
        return out;
    }

    // Deletes the music folder copies of songs that are already elsewhere (a theme, or an earlier music folder song):
    // only their cafe_barista_men.bfsar and music-volume.txt (moved to the copy that's kept if that one has none), and
    // the folder if nothing else is left in it. Returns true if anything was deleted.
    bool DeleteDuplicates(const std::vector<Song> &songs) {
        bool any = false;
        for (const Song &s : songs) {
            const Source *keep = s.copies.front();
            for (size_t i = 1; i < s.copies.size(); i++) {
                const Source *c = s.copies[i];
                if (!c->theme.empty()) {
                    continue; // themes are never touched
                }
                if (IsFile(c->volume) && !IsFile(keep->volume)) {
                    if (rename(c->volume.c_str(), keep->volume.c_str()) == 0) {
                        SDLOG("music: %s: music-volume.txt moved to %s", c->name.c_str(), keep->name.c_str());
                    }
                }
                const bool gone = unlink(c->archive.c_str()) == 0;
                unlink(c->volume.c_str());
                rmdir(c->folder.c_str()); // only works when it's empty
                unlink(Cache("records/" + c->name + ".txt").c_str());
                SDLOG("music: %s is the same song as %s: %s", c->name.c_str(), keep->name.c_str(),
                      gone ? "deleted from the music folder" : "COULDN'T DELETE");
                any = any || gone;
            }
        }
        return any;
    }

    // Deletes what's in the cache that nothing uses any more: song streams, menus and records.
    void DeleteUnused(const std::vector<Source> &sources, const std::vector<Song> &songs, const std::set<std::string> &menus) {
        for (const auto &f : Files(Cache("songs"))) {
            const bool used = std::any_of(songs.begin(), songs.end(), [&](const Song &s) { return s.stream == Cache("songs/" + f); });
            if (!used) {
                unlink(Cache("songs/" + f).c_str());
                SDLOG("music: songs/%s deleted (nothing uses it)", f.c_str());
            }
        }
        for (const auto &f : Files(Cache("menus"))) {
            if (!menus.count(f.substr(0, f.size() - (f.size() > 6 ? 6 : 0)))) {
                unlink(Cache("menus/" + f).c_str());
                unlink(Cache("records/menu - " + f.substr(0, f.size() - 6) + ".txt").c_str());
                SDLOG("music: menus/%s deleted (nothing uses it)", f.c_str());
            }
        }
        for (const auto &f : Files(Cache("records"))) {
            const std::string name = f.size() > 4 ? f.substr(0, f.size() - 4) : f;
            if (name.rfind("menu - ", 0) == 0) {
                continue; // done with the menus above
            }
            if (std::none_of(sources.begin(), sources.end(), [&](const Source &s) { return s.name == name; })) {
                unlink(Cache("records/" + f).c_str());
            }
        }
    }

    bool AddFileLayer(const std::string &what, const std::string &target, const std::string &replacement) {
        CRLayerHandle handle = 0;
        const auto res       = ContentRedirection_AddFSLayerEx(&handle, ("PumpkinSpice " + what).c_str(), target.c_str(), replacement.c_str(),
                                                               FS_LAYER_TYPE_EX_REPLACE_FILE);
        if (res != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            SDLOG("music: %s layer failed: %s (%d)", what.c_str(), ContentRedirection_GetStatusStr(res), res);
            DEBUG_FUNCTION_LINE_ERR("Failed to redirect %s to %s", target.c_str(), replacement.c_str());
            return false;
        }
        gLayerHandles.push_back(handle);
        return true;
    }

    void Notify(const char *text) {
        static bool sInit = false;
        if (!sInit) {
            const auto res = NotificationModule_InitLibrary();
            if (res != NOTIFICATION_MODULE_RESULT_SUCCESS) {
                SDLOG("music: NotificationModule not there (%d): no notification", res);
                return;
            }
            sInit = true;
        }
        [[maybe_unused]] const auto res = NotificationModule_AddInfoNotification(text); // only logged
        SDLOG("music: notification \"%s\": %d", text, res);
    }
} // namespace

namespace {
    // The songs ready for this Menu start (PrepareMusic fills it, ApplyShuffledMusic picks from it)
    struct Ready {
        std::string name, stream;
        Volume volume;
    };
    std::vector<Ready> sReady;

    unsigned Ms(OSTime since) {
        return unsigned(OSTicksToMilliseconds(OSGetTime() - since));
    }

    // Everything the prep's result depends on, as text: every source's archive and music-volume.txt, every theme and
    // where its menus come from, and the console's own files (size and time of each). index.txt keeps it with the
    // ready songs, so a Menu start with nothing changed only checks these instead of reading every record.
    std::string Fingerprint(const std::vector<std::string> &themes, const std::vector<Source> &s) {
        std::string f;
        for (const auto &src : s) {
            f += "src\t" + src.name + "\t" + Stamp(src.archive) + "\t" + Stamp(src.volume) + "\n";
        }
        for (const auto &t : themes) {
            const std::string from = MenuSource(t);
            f += "theme\t" + t + "\t" + (IsDir(std::string(THEMES_PATH) + "/" + t) ? (from.empty() ? kDefault : Stamp(from)) : "-") + "\n";
        }
        f += "base\t" + Stamp(Cache("default.bfsar")) + "\t" + Stamp(Cache("BGM_SET_ACS.bfstm")) + "\n";
        return f;
    }

    // index.txt: the fingerprint, a line "--", then a line per ready song: name, stream file, TV and GamePad volume
    bool LoadIndex(const std::string &fingerprint) {
        const std::string t   = ReadText(Cache("index.txt"));
        const std::string sep = "--\n";
        if (t.size() < fingerprint.size() + sep.size() || t.compare(0, fingerprint.size(), fingerprint) != 0 ||
            t.compare(fingerprint.size(), sep.size(), sep) != 0) {
            return false;
        }
        sReady.clear();
        size_t at = fingerprint.size() + sep.size();
        while (at < t.size()) {
            size_t end = t.find('\n', at);
            if (end == std::string::npos) {
                end = t.size();
            }
            const std::string line = t.substr(at, end - at);
            at                     = end + 1;
            const size_t a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
            if (c == std::string::npos) {
                continue;
            }
            Ready r;
            r.name       = line.substr(0, a);
            r.stream     = line.substr(a + 1, b - a - 1);
            r.volume.tv  = uint8_t(std::atoi(line.c_str() + b + 1));
            r.volume.drc = uint8_t(std::atoi(line.c_str() + c + 1));
            sReady.push_back(r);
        }
        return true;
    }

    // Every song gets a music-volume.txt to change its volume with, next to the copy that sets it (the first: a theme's
    // folder, or the music folder song's own). One that's there is never changed. Written straight to its place, not
    // through WriteAll, which only makes folders inside the cache.
    void WriteVolumeFiles(const std::vector<Song> &songs) {
        for (const Song &s : songs) {
            const Source *keep = s.copies.front();
            if (IsFile(keep->volume)) {
                continue;
            }
            const std::string text = "# Shuffle music volume for this song: TV, then GamePad (0 to 127, or a\n"
                                     "# percentage of full volume like 19%).\n"
                                     "# Without this file a song plays at 44 41 (35% 32%).\n"
                                     "#   44 41 (35% 32%)   the default, for most songs\n"
                                     "#   24 22 (19% 17%)   songs that are naturally loud, like original soundtrack files\n" +
                                     std::to_string(s.volume.tv) + " " + std::to_string(s.volume.drc) + "\n";
            FILE *f = fopen(keep->volume.c_str(), "wb");
            if (!f) {
                SDLOG("music: couldn't write %s", keep->volume.c_str());
                continue;
            }
            [[maybe_unused]] const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size(); // only logged
            fclose(f);
            SDLOG("music: %s: music-volume.txt %s", keep->name.c_str(), ok ? "added" : "NOT written");
        }
    }

    void SaveIndex(const std::string &fingerprint, const std::vector<Song> &songs) {
        sReady.clear();
        std::string t = fingerprint + "--\n";
        for (const Song &s : songs) {
            sReady.push_back({s.name, s.stream, s.volume});
            t += s.name + "\t" + s.stream + "\t" + std::to_string(s.volume.tv) + "\t" + std::to_string(s.volume.drc) + "\n";
        }
        WriteText(Cache("index.txt"), t);
    }
} // namespace

void PrepareMusic(const std::vector<std::string> &themes) {
    [[maybe_unused]] const OSTime start = OSGetTime(); // only logged
    std::vector<Source> s               = Sources(themes);
    const bool baseMissing              = !IsFile(Cache("default.bfsar")) || !IsFile(Cache("BGM_SET_ACS.bfstm"));
    if (!baseMissing && LoadIndex(Fingerprint(themes, s))) {
        SDLOG("music: nothing changed, %d songs ready (%u ms)", (int) sReady.size(), Ms(start));
        return;
    }
    sReady.clear();

    // songs to read: no record, or the archive changed since
    std::vector<const Source *> todo;
    for (const auto &src : s) {
        // a record from before songs had ids and lengths (the first v1.5 test build) is done again too
        const auto r = ReadRecord(src.name, 4);
        if (r[0] != Stamp(src.archive) || (r[1] != "none" && (r[2].empty() || r[3].empty()))) {
            todo.push_back(&src);
        }
    }
    AppHeapFallback appHeap; // the archives are about 10 MB each and get copied a few times while they're changed
    bool screen = false;
    int total = 0, n = 0;
    auto progress = [&](int add) {
        if (!screen) {
            screen = ScreenInit();
        }
        total += add;
        if (screen) {
            DrawPreparing(n, total);
        }
    };
    if (!todo.empty() || baseMissing) {
        SDLOG("music: %d song(s) to read%s", (int) todo.size(), baseMissing ? ", and the console's own files" : "");
        progress(int(todo.size()));
    }
    // the console's own archive and stream, read before any layer is in place
    if (baseMissing) {
        Bytes b;
        if (!ReadAll(kConsoleArchive, b) || !WriteAll(Cache("default.bfsar"), b.data(), b.size()) || !ReadAll(kConsoleTemplate, b) ||
            !WriteAll(Cache("BGM_SET_ACS.bfstm"), b.data(), b.size())) {
            SDLOG("music: couldn't copy the console's own sound files: Shuffle music stays off this time");
            ScreenDeInit();
            return;
        }
    }
    if (!todo.empty()) {
        Bytes tpl;
        if (!ReadAll(Cache("BGM_SET_ACS.bfstm"), tpl)) {
            SDLOG("music: couldn't read BGM_SET_ACS.bfstm");
            ScreenDeInit();
            return;
        }
        for (const Source *src : todo) {
            n++;
            if (screen) {
                DrawPreparing(n, total);
            }
            const std::string stamp = Stamp(src->archive);
            SongInfo song;
            try {
                Bytes sar;
                if (!ReadAll(src->archive, sar)) {
                    throw FormatError("couldn't read " + kArchive);
                }
                song = MakeSong(*src, sar, tpl);
            } catch (const std::exception &e) {
                SDLOG("music: %s: %s", src->name.c_str(), e.what());
            }
            WriteRecord(src->name, {stamp, song.kind, song.id, std::to_string(song.ms)});
            SDLOG("music: %s: %s %s %u ms", src->name.c_str(), song.kind.c_str(), song.id.c_str(), (unsigned) song.ms);
        }
    }

    // the same song in several places: one song, the music folder's extra copies deleted
    std::vector<Song> songs = Songs(s);
    if (DeleteDuplicates(songs)) {
        s     = Sources(themes);
        songs = Songs(s);
    }

    // menus: every theme (and the console's own archive, for themes without one) at every volume a song uses
    std::set<Volume> volumes;
    for (const auto &song : songs) {
        volumes.insert(song.volume);
    }
    std::set<std::string> needed;
    std::vector<std::pair<std::string, Volume>> menus; // theme, volume
    for (const auto &t : themes) {
        if (!IsDir(std::string(THEMES_PATH) + "/" + t)) {
            continue; // a package that isn't a theme folder yet
        }
        const std::string from = MenuSource(t).empty() ? Cache("default.bfsar") : MenuSource(t);
        for (const Volume &v : volumes) {
            const std::string name = MenuName(t, v);
            if (needed.insert(name).second && (ReadRecord("menu - " + name, 1)[0] != Stamp(from) || !IsFile(Cache("menus/" + name + ".bfsar")))) {
                menus.push_back({t, v});
            }
        }
    }
    if (!menus.empty()) {
        SDLOG("music: %d sound archive(s) to make", (int) menus.size());
        progress(int(menus.size()));
    }
    std::string loaded; // the archive in `sar`, so each theme is read once for all its volumes
    Bytes sar;
    for (const auto &[theme, v] : menus) {
        n++;
        if (screen) {
            DrawPreparing(n, total);
        }
        const std::string from = MenuSource(theme).empty() ? Cache("default.bfsar") : MenuSource(theme);
        const std::string name = MenuName(theme, v);
        try {
            if (loaded != from) {
                loaded.clear();
                if (!ReadAll(from, sar)) {
                    throw FormatError("couldn't read " + from);
                }
                loaded = from;
            }
            MakeMenu(name, sar, v);
            WriteRecord("menu - " + name, {Stamp(from)});
            SDLOG("music: menu %s ready", name.c_str());
        } catch (const std::exception &e) {
            SDLOG("music: menu %s: %s", name.c_str(), e.what());
            WriteRecord("menu - " + name, {"failed"});
        }
    }
    DeleteUnused(s, songs, needed);
    WriteVolumeFiles(songs);
    SaveIndex(Fingerprint(themes, s), songs);
    if (screen) {
        ScreenDeInit();
    }
    SDLOG("music: %d songs ready in %u ms", (int) sReady.size(), Ms(start));
}

void ApplyShuffledMusic(const std::string &theme, const std::vector<std::string> &themes) {
    const std::string target = "/vol/content/Common/Sound/Men/";
    // the easter egg: the console's own music and sound effects
    if (IsFile(Cache("default.bfsar")) && ShuffleRandom(kEasterEggChance) == 0) {
        if (AddFileLayer("default sound", target + kArchive, Cache("default.bfsar"))) {
            SDLOG("music: the default music easter egg");
            Notify("PumpkinSpice: You found the default music easter egg!");
        }
        return;
    }

    // a song from the ready list (PrepareMusic, this Menu start). Only the picked song's two files are checked: one
    // that's gone (deleted by hand) is left out and the index is dropped, so the next start looks at everything again.
    (void) themes;
    const OSTime start       = OSGetTime();
    std::vector<Ready> ready = sReady;
    while (!ready.empty()) {
        std::vector<std::string> names;
        for (const auto &r : ready) {
            names.push_back(r.name);
        }
        const std::string pick = ShufflePick(names, gMusicHistory, "music");
        const auto it          = std::find_if(ready.begin(), ready.end(), [&](const Ready &r) { return r.name == pick; });
        const std::string menu = Cache("menus/" + MenuName(theme, it->volume) + ".bfsar");
        if (!IsFile(it->stream) || !IsFile(menu)) {
            SDLOG("music: \"%s\" isn't ready after all (%s missing)", pick.c_str(), IsFile(it->stream) ? menu.c_str() : it->stream.c_str());
            unlink(Cache("index.txt").c_str());
            ready.erase(it);
            continue;
        }
        [[maybe_unused]] const unsigned picked = Ms(start); // the timings are only logged
        if (!AddFileLayer("song", target + "stream/BGM_MEN.bfstm", it->stream)) {
            return; // no stream file: the theme's own archive and music stay
        }
        [[maybe_unused]] const unsigned song = Ms(start);
        if (!AddFileLayer("sound archive", target + kArchive, menu)) {
            SDLOG("music: the theme's own archive and music stay");
            return;
        }
        // stored now, saved to the SD card when the Menu closes (SaveMusicHistory), not while it's starting
        RememberShuffled(MUSIC_HISTORY_STRING, gMusicHistory, pick, false);
        SDLOG("music: \"%s\" over \"%s\" (%s; picked %u ms, song layer %u ms, archive layer %u ms)", pick.c_str(), theme.c_str(),
              menu.c_str(), picked, song - picked, Ms(start) - song);
        return;
    }
    SDLOG("music: nothing ready for \"%s\": its own music this time", theme.c_str());
}

void SaveMusicHistory() {
    WUPSStorageAPI::SaveStorage();
}
