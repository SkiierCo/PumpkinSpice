#include "wiiumenu/bfsar.hpp"
#include "wiiumenu/bfstm.hpp"

#include <algorithm>
#include <functional>
#include <cstring>
#include <map>

namespace wiiumenu::bfsar {
namespace {

struct R {
    ByteView b;
    std::uint8_t u8(std::size_t o) const { if (o >= b.size()) throw FormatError("BFSAR read past end"); return b[o]; }
    std::uint16_t u16(std::size_t o) const { return read_u16(b, o, true); }
    std::uint32_t u32(std::size_t o) const { return read_u32(b, o, true); }
    std::int32_t s32(std::size_t o) const { return std::int32_t(read_u32(b, o, true)); }
    std::string magic(std::size_t o) const {
        if (o + 4 > b.size()) return "";
        return std::string(reinterpret_cast<const char*>(b.data() + o), 4);
    }
};

// Reference table: u32 count, then {u16 type, u16 pad, s32 offset} relative to the table.
std::vector<std::size_t> table(const R& r, std::size_t t) {
    std::vector<std::size_t> v;
    const std::uint32_t n = r.u32(t);
    if (n > 100000) throw FormatError("BFSAR table too large");
    for (std::uint32_t i = 0; i < n; ++i) v.push_back(t + std::size_t(r.s32(t + 4 + 8 * i + 4)));
    return v;
}

// Option block: u32 flags, then one u32 per set bit (bit 0 = string id).
std::map<int, std::uint32_t> options(const R& r, std::size_t o) {
    std::map<int, std::uint32_t> m;
    const std::uint32_t flags = r.u32(o);
    std::size_t p = o + 4;
    for (int bit = 0; bit < 32; ++bit)
        if (flags & (1u << bit)) { m[bit] = r.u32(p); p += 4; }
    return m;
}

std::map<std::uint16_t, std::size_t> sections(const R& r, std::size_t base) {
    std::map<std::uint16_t, std::size_t> s;
    const std::uint16_t n = r.u16(base + 0x10);
    for (std::uint16_t i = 0; i < n; ++i) s[r.u16(base + 0x14 + 12 * i)] = base + r.u32(base + 0x18 + 12 * i);
    return s;
}

struct Layout {
    std::size_t info_body = 0, file_block = 0;
    std::map<std::uint16_t, std::size_t> refs;
};

Layout layout(const R& r) {
    if (r.magic(0) != "FSAR" || r.u16(4) != 0xFEFF) throw FormatError("not a big-endian BFSAR");
    const auto s = sections(r, 0);
    if (!s.count(0x2000) || !s.count(0x2001) || !s.count(0x2002)) throw FormatError("BFSAR blocks missing");
    Layout l;
    l.info_body = s.at(0x2001) + 8;
    l.file_block = s.at(0x2002);
    for (int k = 0; k < 8; ++k) l.refs[r.u16(l.info_body + 8 * std::size_t(k))] = l.info_body + std::size_t(r.s32(l.info_body + 8 * std::size_t(k) + 4));
    return l;
}

}  // namespace

bool is_bfsar(ByteView d) { return d.size() >= 0x40 && std::memcmp(d.data(), "FSAR", 4) == 0; }

const char* type_name(SoundType t) {
    switch (t) {
        case SoundType::Stream: return "stream";
        case SoundType::Wave: return "wave";
        case SoundType::Sequence: return "sequence";
        default: return "unknown";
    }
}

Archive read(ByteView data) {
    const R r{data};
    const Layout l = layout(r);
    Archive a;

    const std::size_t strg = sections(r, 0).at(0x2000) + 8;
    const std::size_t st = strg + std::size_t(r.s32(strg + 4));
    const std::uint32_t ns = r.u32(st);
    for (std::uint32_t i = 0; i < ns; ++i) {
        const std::size_t e = st + 4 + 12 * i;
        const std::size_t off = st + std::size_t(r.s32(e + 4));
        const std::uint32_t len = r.u32(e + 8);
        if (!len || off + len > data.size()) throw FormatError("bad BFSAR string");
        a.strings.emplace_back(reinterpret_cast<const char*>(data.data() + off), len - 1);
    }
    auto name = [&](const std::map<int, std::uint32_t>& o) -> std::string {
        auto it = o.find(0);
        return it != o.end() && it->second < a.strings.size() ? a.strings[it->second] : std::string();
    };

    for (std::size_t o : table(r, l.refs.at(0x2102))) a.players.push_back(name(options(r, o + 4)));

    for (std::size_t o : table(r, l.refs.at(0x2106))) {
        File f;
        f.index = std::uint32_t(a.files.size());
        const std::uint16_t ty = r.u16(o);
        const std::size_t ro = o + std::size_t(r.s32(o + 4));
        if (ty == 0x220D) {
            f.external = true;
            std::size_t z = ro;
            while (z < data.size() && data[z]) ++z;
            f.path.assign(reinterpret_cast<const char*>(data.data() + ro), z - ro);
        } else if (ty == 0x220C) {
            const std::int32_t off = r.s32(ro + 4);
            f.size = r.u32(ro + 8);
            if (off >= 0) {
                f.offset = l.file_block + 8 + std::size_t(off);
                f.magic = r.magic(*f.offset);
            }
        }
        a.files.push_back(f);
    }

    for (std::size_t o : table(r, l.refs.at(0x2101)))
        a.banks.push_back({std::uint32_t(a.banks.size()), name(options(r, o + 12)), r.u32(o)});
    for (std::size_t o : table(r, l.refs.at(0x2103)))
        a.wave_archives.push_back({std::uint32_t(a.wave_archives.size()), name(options(r, o + 8)), r.u32(o)});
    for (std::size_t o : table(r, l.refs.at(0x2105))) {
        a.groups.push_back({std::uint32_t(a.groups.size()), name(options(r, o + 4)), r.u32(o)});
        std::vector<GroupItem> items;
        const File& gf = a.files.at(r.u32(o));
        if (gf.offset && gf.magic == "FGRP") {
            const auto gs = sections(r, *gf.offset);
            const std::size_t info = gs.at(0x7800), fs = gs.at(0x7801);
            for (std::size_t e : table(r, info + 8)) {
                GroupItem it;
                it.file = r.u32(e);
                it.size = r.u32(e + 12);
                const std::size_t pos = fs + 8 + std::size_t(r.s32(e + 8));
                it.magic = r.magic(pos);
                if (it.magic == "FWAR") it.wave_count = r.u32(sections(r, pos).at(0x6800) + 8);
                items.push_back(it);
            }
        }
        a.group_items.push_back(std::move(items));
    }

    const std::size_t lp = l.refs.at(0x220B);
    a.limits = {r.u16(lp), r.u16(lp + 2), r.u16(lp + 4), r.u16(lp + 6), r.u16(lp + 8), r.u16(lp + 10), r.u16(lp + 12)};

    for (std::size_t o : table(r, l.refs.at(0x2100))) {
        Sound s;
        s.index = std::uint32_t(a.sounds.size());
        s.entry_offset = o;
        s.file = r.u32(o);
        s.player = r.u32(o + 4) & 0xFFFFFF;
        s.volume = r.u8(o + 8);
        const std::uint16_t dty = r.u16(o + 0x0C);
        s.detail_offset = o + std::size_t(r.s32(o + 0x10));
        s.name = name(options(r, o + 0x14));
        const std::size_t d = s.detail_offset;
        if (dty == 0x2201) {
            s.type = SoundType::Stream;
            s.track_mask = r.u16(d);
            s.stream_channels = r.u16(d + 2);
        } else if (dty == 0x2202) {
            s.type = SoundType::Wave;
            s.wave_index = r.u32(d);
        } else if (dty == 0x2203) {
            s.type = SoundType::Sequence;
            const std::size_t bt = d + std::size_t(r.s32(d + 4));
            const std::uint32_t nb = r.u32(bt);
            for (std::uint32_t i = 0; i < nb && i < 16; ++i) s.banks.push_back(r.u32(bt + 4 + 4 * i) & 0xFFFFFF);
            s.track_mask = r.u32(d + 8);
        }
        a.sounds.push_back(std::move(s));
    }
    return a;
}

Bytes alias_to_stream(ByteView data, const std::string& sound, const std::string& stream_sound) {
    const Archive a = read(data);
    const Sound* dst = nullptr;
    const Sound* src = nullptr;
    for (const auto& s : a.sounds) {
        if (s.name == sound) dst = &s;
        if (s.name == stream_sound) src = &s;
    }
    if (!dst) throw FormatError("no sound named " + sound);
    if (!src || src->type != SoundType::Stream) throw FormatError(stream_sound + " is not a stream sound");
    Bytes out(data.begin(), data.end());
    auto put32 = [&](std::size_t at, std::uint32_t v) { for (int k = 0; k < 4; ++k) out[at + std::size_t(k)] = std::uint8_t(v >> (24 - 8 * k)); };
    put32(dst->entry_offset, src->file);                                        // same stream file
    out[dst->entry_offset + 0x0C] = 0x22; out[dst->entry_offset + 0x0D] = 0x01;  // detail type: stream
    put32(dst->entry_offset + 0x10, std::uint32_t(std::int64_t(src->detail_offset) - std::int64_t(dst->entry_offset)));
    // verify the patched entry reads back as the intended stream sound
    const Archive check = read(out);
    const Sound& c = check.sounds[dst->index];
    if (c.type != SoundType::Stream || c.file != src->file || c.detail_offset != src->detail_offset)
        throw FormatError("internal error: stream alias did not take");
    return out;
}

Bytes set_stream_limits(ByteView data, std::uint16_t stream_sounds, std::uint16_t stream_tracks, std::uint16_t stream_channels) {
    const R r{data};
    const Layout l = layout(r);
    const std::size_t lp = l.refs.at(0x220B);  // u16: sequence sounds, sequence tracks, stream sounds, stream tracks, stream channels, ...
    Bytes out(data.begin(), data.end());
    auto put16 = [&](std::size_t at, std::uint16_t v) { out[at] = std::uint8_t(v >> 8); out[at + 1] = std::uint8_t(v); };
    put16(lp + 4, stream_sounds);
    put16(lp + 6, stream_tracks);
    put16(lp + 8, stream_channels);
    const Archive c = read(out);
    if (c.limits.stream_sounds != stream_sounds || c.limits.stream_tracks != stream_tracks || c.limits.stream_channels != stream_channels)
        throw FormatError("internal error: limits did not read back");
    return out;
}

Bytes add_stream(ByteView data, const std::string& sound, const std::string& template_stream, const std::string& path,
                 const std::vector<std::string>& mute) {
    const R r{data};
    const Layout l = layout(r);
    const Archive a = read(data);
    const Sound* dst = nullptr;
    const Sound* src = nullptr;
    for (const auto& s : a.sounds) {
        if (s.name == sound) dst = &s;
        if (s.name == template_stream) src = &s;
    }
    if (!dst) throw FormatError("no sound named " + sound);
    if (!src || src->type != SoundType::Stream) throw FormatError(template_stream + " is not a stream sound");
    std::size_t ext_info = 0;  // an existing external file info, copied for the new one
    for (std::size_t o : table(r, l.refs.at(0x2106))) if (r.u16(o) == 0x220D) { ext_info = o; break; }
    if (!ext_info) throw FormatError("archive has no external file to copy");

    // block table
    const auto blocks = sections(r, 0);
    const std::size_t info = blocks.at(0x2001), file_blk = blocks.at(0x2002);
    const std::uint32_t info_size = r.u32(info + 4);
    const std::size_t body = info + 8;

    // new data appended to INFO: file table with one more entry, then the new file info
    const std::size_t old_table = l.refs.at(0x2106);
    const std::uint32_t nfiles = r.u32(old_table);
    const std::uint16_t entry_type = r.u16(old_table + 4);
    const std::size_t new_table = info + ((info_size + 3) & ~std::size_t(3));
    const std::size_t new_info = new_table + 4 + 8 * (std::size_t(nfiles) + 1);
    Bytes add(new_info - new_table, 0);
    auto put = [](Bytes& b, std::size_t at, std::uint32_t v) { for (int k = 0; k < 4; ++k) b[at + std::size_t(k)] = std::uint8_t(v >> (24 - 8 * k)); };
    auto put16 = [](Bytes& b, std::size_t at, std::uint16_t v) { b[at] = std::uint8_t(v >> 8); b[at + 1] = std::uint8_t(v); };
    put(add, 0, nfiles + 1);
    for (std::uint32_t i = 0; i <= nfiles; ++i) {
        const std::size_t target = i < nfiles ? old_table + std::size_t(r.s32(old_table + 4 + 8 * i + 4)) : new_info;
        put16(add, 4 + 8 * std::size_t(i), entry_type);
        put(add, 4 + 8 * std::size_t(i) + 4, std::uint32_t(std::int64_t(target) - std::int64_t(new_table)));
    }
    const std::size_t path_rel = std::size_t(r.s32(ext_info + 4));          // where the path sits inside a file info
    Bytes fi(data.begin() + std::ptrdiff_t(ext_info), data.begin() + std::ptrdiff_t(ext_info + path_rel));
    fi.insert(fi.end(), path.begin(), path.end());
    fi.push_back(0);
    while (fi.size() % 4) fi.push_back(0);
    add.insert(add.end(), fi.begin(), fi.end());

    // The template's stream info normally carries a track table (which channels make each track, its volume and pan).
    // Some edited archives keep only the first 4 bytes (track mask and channel count): the iiSU theme's BGM_SET_ACS
    // (2026-11-13), saved by another tool. A stream sound without the table plays nothing, so the new sound then gets
    // its own full stream info with the stock BGM_SET_ACS values: track 0 = channels 0-1 at volume 23, track 1 =
    // channels 2-3 at volume 127, pan 64, flag 1. The archive's own BGM_SET_ACS is left as it is.
    std::size_t own_detail = 0;  // position of the new stream info in `add`, 0 = use the template's
    if (r.u16(src->detail_offset + 4) != 0x0101) {
        if (src->track_mask != 3 || src->stream_channels != 4)
            throw FormatError(template_stream + " has no track table and isn't the stock 2-track, 4-channel layout");
        own_detail = add.size();
        const std::uint8_t stock[0x4C] = {
            0x00, 0x03, 0x00, 0x04, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,  // mask, channels, table ref
            0x00, 0x00, 0x00, 0x02, 0x22, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x22, 0x0E, 0x00, 0x00,  // 2 tracks
            0x00, 0x00, 0x00, 0x28, 0x17, 0x40, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C,  // track 0
            0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x7F, 0x40, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,  // channels 0, 1; track 1
            0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x00, 0x00};                          // channels 2, 3
        add.insert(add.end(), std::begin(stock), std::end(stock));
    }

    // grow INFO so the FILE block moves by a multiple of 4 KB (all data alignments are kept)
    const std::size_t grown_end = new_table + add.size();
    std::size_t delta = ((grown_end - (info + info_size)) + 0xFFF) & ~std::size_t(0xFFF);
    const std::size_t new_info_size = info_size + delta;
    Bytes out(data.begin(), data.begin() + std::ptrdiff_t(info + info_size));
    out.resize(info + new_info_size, 0);
    std::copy(add.begin(), add.end(), out.begin() + std::ptrdiff_t(new_table));
    out.insert(out.end(), data.begin() + std::ptrdiff_t(info + info_size), data.end());

    // INFO size, file table reference, header block table, file size
    put(out, info + 4, std::uint32_t(new_info_size));
    for (int k = 0; k < 8; ++k)
        if (r.u16(body + 8 * std::size_t(k)) == 0x2106) put(out, body + 8 * std::size_t(k) + 4, std::uint32_t(new_table - body));
    const std::uint16_t nb = r.u16(0x10);
    for (std::uint16_t i = 0; i < nb; ++i) {
        const std::size_t e = 0x14 + 12 * std::size_t(i);
        const std::uint16_t ty = r.u16(e);
        if (ty == 0x2001) put(out, e + 8, std::uint32_t(new_info_size));
        if (ty == 0x2002) put(out, e + 4, std::uint32_t(file_blk + delta));
    }
    put(out, 0x0C, std::uint32_t(out.size()));

    // the sound: stream of the new file, with the template stream's settings (or the full stream info added above)
    put(out, dst->entry_offset, nfiles);
    out[dst->entry_offset + 0x0C] = 0x22; out[dst->entry_offset + 0x0D] = 0x01;
    const std::size_t detail = own_detail ? new_table + own_detail : src->detail_offset;
    put(out, dst->entry_offset + 0x10, std::uint32_t(std::int64_t(detail) - std::int64_t(dst->entry_offset)));
    for (const auto& m : mute) {
        const Sound* s = nullptr;
        for (const auto& x : a.sounds) if (x.name == m) s = &x;
        if (!s) throw FormatError("no sound named " + m);
        out[s->entry_offset + 8] = 0;  // volume
    }

    // verify
    const Archive c = read(out);
    if (c.files.size() != a.files.size() + 1 || !c.files.back().external || c.files.back().path != path)
        throw FormatError("internal error: new file entry did not read back");
    const Sound& cs = c.sounds[dst->index];
    if (cs.type != SoundType::Stream || cs.file != nfiles || cs.track_mask != src->track_mask || cs.stream_channels != src->stream_channels)
        throw FormatError("internal error: stream sound did not read back");
    if (cs.detail_offset != detail || (out[detail + 4] << 8 | out[detail + 5]) != 0x0101)
        throw FormatError("internal error: stream sound has no track table");
    for (std::size_t i = 0; i < a.files.size(); ++i)
        if (c.files[i].external != a.files[i].external || c.files[i].path != a.files[i].path || c.files[i].size != a.files[i].size ||
            c.files[i].magic != a.files[i].magic)
            throw FormatError("internal error: file " + std::to_string(i) + " changed");
    return out;
}

Bytes set_sound_volume(ByteView data, const std::string& sound, std::uint8_t volume) {
    const Archive a = read(data);
    const Sound* s = nullptr;
    for (const auto& x : a.sounds) if (x.name == sound) s = &x;
    if (!s) throw FormatError("no sound named " + sound);
    Bytes out(data.begin(), data.end());
    out[s->entry_offset + 8] = volume;
    // verify: only this sound's volume changed
    const Archive c = read(out);
    for (std::size_t i = 0; i < a.sounds.size(); ++i)
        if (c.sounds[i].volume != (i == s->index ? volume : a.sounds[i].volume) || c.sounds[i].name != a.sounds[i].name)
            throw FormatError("internal error: volume of " + a.sounds[i].name + " did not read back");
    return out;
}

Bytes set_sound_player(ByteView data, const std::string& player, const std::vector<std::string>& sounds) {
    const Archive a = read(data);
    std::uint32_t index = std::uint32_t(a.players.size());
    for (std::uint32_t i = 0; i < a.players.size(); ++i) if (a.players[i] == player) index = i;
    if (index == a.players.size()) throw FormatError("no player named " + player);
    std::vector<const Sound*> moved;
    for (const auto& n : sounds) {
        const Sound* s = nullptr;
        for (const auto& x : a.sounds) if (x.name == n) s = &x;
        if (!s) throw FormatError("no sound named " + n);
        moved.push_back(s);
    }
    Bytes out(data.begin(), data.end());
    auto put = [](Bytes& b, std::size_t at, std::uint32_t v) { for (int k = 0; k < 4; ++k) b[at + std::size_t(k)] = std::uint8_t(v >> (24 - 8 * k)); };
    const R r{data};
    // the sound's player reference: same item id type byte, other player index
    for (const Sound* s : moved) put(out, s->entry_offset + 4, (r.u32(s->entry_offset + 4) & 0xFF000000u) | index);
    // verify: only the moved sounds' player changed
    const Archive c = read(out);
    for (std::size_t i = 0; i < a.sounds.size(); ++i) {
        bool m = false;
        for (const Sound* s : moved) if (s->index == i) m = true;
        if (c.sounds[i].player != (m ? index : a.sounds[i].player) || c.sounds[i].name != a.sounds[i].name)
            throw FormatError("internal error: sound " + a.sounds[i].name + " did not read back");
    }
    return out;
}

namespace {

void put32(Bytes& b, std::size_t at, std::uint32_t v) { for (int k = 0; k < 4; ++k) b.at(at + std::size_t(k)) = std::uint8_t(v >> (24 - 8 * k)); }
void add32(Bytes& b, std::size_t at, std::int64_t d) { put32(b, at, std::uint32_t(std::int64_t(read_u32(b, at, true)) + d)); }

// Header section table of a Cafe file (FSAR, FGRP, FWAR, FWAV): {u16 type, u16 pad, u32 offset, u32 size} at 0x14.
struct Sec { std::size_t entry; std::uint16_t type; std::size_t offset; std::uint32_t size; };
std::vector<Sec> section_table(const R& r, std::size_t base) {
    std::vector<Sec> v;
    const std::uint16_t n = r.u16(base + 0x10);
    for (std::uint16_t i = 0; i < n; ++i) {
        const std::size_t e = base + 0x14 + 12 * std::size_t(i);
        v.push_back({e, r.u16(e), base + r.u32(e + 4), r.u32(e + 8)});
    }
    return v;
}

// New BFWAV with the template wave's channel count, laid out like the Menu's own waves:
// header (0x40) | INFO: encoding, rate, loop start, sample count, original loop start, channel reference table,
// channel infos (0x14 each), DSP-ADPCM infos (0x2E each, ADPCM only), 32-byte aligned | DATA: 0x18 bytes, then
// each channel's samples, channels 32-byte aligned.
Bytes build_wave(const R& r, std::size_t tpl, std::uint32_t rate, const std::vector<std::vector<std::int16_t>>& in, bool adpcm,
                 std::uint32_t channels_out = 0) {
    if (in.empty() || in[0].empty()) throw FormatError("the new audio is empty");
    std::size_t info = 0;
    for (const Sec& s : section_table(r, tpl))
        if (s.type == 0x7000) info = s.offset;
    if (r.magic(tpl) != "FWAV" || !info) throw FormatError("wave to replace is not a BFWAV");
    const std::uint32_t nch = channels_out ? channels_out : r.u32(info + 8 + 0x14);
    if (nch < 1 || nch > 2) throw FormatError("wave to replace has " + std::to_string(nch) + " channels");
    const std::size_t frames = in[0].size();
    std::vector<std::vector<std::int16_t>> ch(nch, std::vector<std::int16_t>(frames));
    for (std::size_t i = 0; i < frames; ++i) {
        if (nch == 1) {  // mono: average of all input channels
            std::int32_t sum = 0;
            for (const auto& c : in) sum += c[i];
            ch[0][i] = std::int16_t(sum / std::int32_t(in.size()));
        } else {          // stereo: first two input channels, or the one channel twice
            ch[0][i] = in[0][i];
            ch[1][i] = in.size() > 1 ? in[1][i] : in[0][i];
        }
    }
    std::vector<bfstm::AdpcmChannel> enc;
    std::vector<Bytes> samples;
    for (std::uint32_t c = 0; c < nch; ++c) {
        if (adpcm) {
            enc.push_back(bfstm::encode_adpcm(ch[c], 0, 0x3800));
            samples.push_back(enc.back().data);
        } else {
            Bytes s(frames * 2);
            for (std::size_t i = 0; i < frames; ++i) { s[2 * i] = std::uint8_t(std::uint16_t(ch[c][i]) >> 8); s[2 * i + 1] = std::uint8_t(ch[c][i]); }
            samples.push_back(std::move(s));
        }
    }
    auto align32 = [](std::size_t v) { return (v + 0x1F) & ~std::size_t(0x1F); };
    auto put16 = [](Bytes& b, std::size_t at, std::uint16_t v) { b.at(at) = std::uint8_t(v >> 8); b.at(at + 1) = std::uint8_t(v); };
    const std::size_t table = 0x14, chinfo0 = table + 4 + 8 * nch, adpcm0 = chinfo0 + 0x14 * nch;
    const std::size_t body_size = adpcm ? adpcm0 + 0x2E * nch : adpcm0;
    const std::size_t info_size = align32(8 + body_size);
    const std::size_t stride = align32(samples[0].size());
    const std::size_t data_size = 8 + 0x18 + (nch - 1) * stride + samples.back().size();
    const std::size_t info_at = 0x40, data_at = info_at + info_size;
    Bytes w(data_at + data_size, 0);
    // header
    std::memcpy(w.data(), "FWAV", 4);
    put16(w, 4, 0xFEFF); put16(w, 6, 0x40);
    put32(w, 8, r.u32(tpl + 8));  // version, as the template
    put32(w, 0x0C, std::uint32_t(w.size()));
    put16(w, 0x10, 2);
    put16(w, 0x14, 0x7000); put32(w, 0x18, std::uint32_t(info_at)); put32(w, 0x1C, std::uint32_t(info_size));
    put16(w, 0x20, 0x7001); put32(w, 0x24, std::uint32_t(data_at)); put32(w, 0x28, std::uint32_t(data_size));
    // INFO
    const std::size_t b = info_at + 8;
    std::memcpy(w.data() + info_at, "INFO", 4);
    put32(w, info_at + 4, std::uint32_t(info_size));
    w[b] = adpcm ? 2 : 1;                 // encoding; loop flag 0
    put32(w, b + 4, rate);
    put32(w, b + 0x0C, std::uint32_t(frames));
    put32(w, b + table, nch);
    for (std::uint32_t c = 0; c < nch; ++c) {
        const std::size_t ci = chinfo0 + 0x14 * c;
        put16(w, b + table + 4 + 8 * c, 0x7100);
        put32(w, b + table + 8 + 8 * c, std::uint32_t(ci - table));
        put16(w, b + ci, 0x1F00);
        put32(w, b + ci + 4, std::uint32_t(0x18 + c * stride));
        if (adpcm) {
            const std::size_t ai = adpcm0 + 0x2E * c;
            put16(w, b + ci + 8, 0x0300);
            put32(w, b + ci + 0x0C, std::uint32_t(ai - ci));
            const bfstm::AdpcmChannel& e = enc[c];
            for (int k = 0; k < 16; ++k) put16(w, b + ai + 2 * std::size_t(k), std::uint16_t(e.coefs[k]));
            put16(w, b + ai + 0x20, e.pred_scale);
            put16(w, b + ai + 0x22, std::uint16_t(e.hist1));
            put16(w, b + ai + 0x24, std::uint16_t(e.hist2));
            put16(w, b + ai + 0x26, e.loop_pred_scale);
            put16(w, b + ai + 0x28, std::uint16_t(e.loop_hist1));
            put16(w, b + ai + 0x2A, std::uint16_t(e.loop_hist2));
        } else {
            put32(w, b + ci + 0x0C, 0xFFFFFFFF);  // no ADPCM info
        }
    }
    // DATA
    std::memcpy(w.data() + data_at, "DATA", 4);
    put32(w, data_at + 4, std::uint32_t(data_size));
    for (std::uint32_t c = 0; c < nch; ++c)
        std::copy(samples[c].begin(), samples[c].end(), w.begin() + std::ptrdiff_t(data_at + 8 + 0x18 + c * stride));
    return w;
}

struct WarcCopy { std::size_t fwar; std::size_t group; };  // group: FGRP holding it, 0 if directly in FILE

std::vector<WarcCopy> warc_copies(const R& r, std::uint32_t file_id) {
    const Layout l = layout(r);
    std::vector<WarcCopy> v;
    const std::vector<std::size_t> infos = table(r, l.refs.at(0x2106));
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const std::size_t o = infos[i];
        if (r.u16(o) != 0x220C) continue;
        const std::size_t ro = o + std::size_t(r.s32(o + 4));
        const std::int32_t off = r.s32(ro + 4);
        if (off < 0) continue;
        const std::size_t pos = l.file_block + 8 + std::size_t(off);
        if (i == file_id && r.magic(pos) == "FWAR") v.push_back({pos, 0});
        if (r.magic(pos) == "FGRP") {
            const auto gs = sections(r, pos);
            const std::size_t gi = gs.at(0x7800), gf = gs.at(0x7801);
            for (std::size_t e : table(r, gi + 8))
                if (r.u32(e) == file_id) v.push_back({gf + 8 + std::size_t(r.s32(e + 8)), pos});
        }
    }
    std::sort(v.begin(), v.end(), [](const WarcCopy& a, const WarcCopy& b) { return a.fwar < b.fwar; });
    return v;
}

// Splice `wave_bytes` over wave `wave` of the FWAR at `c.fwar` and fix every offset and size that spans the cut.
Bytes splice_wave(ByteView in, const WarcCopy& c, std::uint32_t wave, const Bytes& wave_bytes) {
    const R r{in};
    const Layout l = layout(r);
    const auto ws = sections(r, c.fwar);
    const std::size_t winfo = ws.at(0x6800), wbody = ws.at(0x6801) + 8;
    const std::uint32_t n = r.u32(winfo + 8);
    if (wave >= n) throw FormatError("wave " + std::to_string(wave) + " out of range (archive has " + std::to_string(n) + ")");
    auto entry = [&](std::uint32_t i) { return winfo + 12 + 12 * std::size_t(i); };
    // empty wave slots have offset -1 (no data), and edited archives don't always store waves in number order,
    // so the old wave's span runs to the nearest wave stored after it, not to wave + 1
    const std::int32_t own = r.s32(entry(wave) + 4);
    if (own < 0) throw FormatError("wave " + std::to_string(wave) + " is an empty slot (no data to replace)");
    const std::size_t start = wbody + std::size_t(own);
    const std::size_t old_size = r.u32(entry(wave) + 8);
    std::int64_t next = -1;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int32_t o = r.s32(entry(i) + 4);
        if (i != wave && o > own && (next < 0 || o < next)) next = o;
    }
    const std::size_t old_span = next >= 0 ? wbody + std::size_t(next) - start : old_size;
    // shift for everything after the old span: a multiple of 32 bytes, so all later data keeps its alignment
    const std::int64_t d = std::int64_t(wave_bytes.size()) - std::int64_t(old_span);
    const std::int64_t s = d >= 0 ? (d + 31) / 32 * 32 : -((-d) / 32 * 32);
    const std::size_t cut = start + old_span;
    Bytes out(in.begin(), in.begin() + std::ptrdiff_t(start));
    out.insert(out.end(), wave_bytes.begin(), wave_bytes.end());
    out.resize(std::size_t(std::int64_t(cut) + s), 0);
    out.insert(out.end(), in.begin() + std::ptrdiff_t(cut), in.end());
    auto after = [&](std::size_t p) { return p >= cut; };
    auto at = [&](std::size_t p) { return after(p) ? std::size_t(std::int64_t(p) + s) : p; };  // old position -> new

    // a Cafe container (FWAR / FGRP / FSAR) holding the cut: file size, its data block's size, later blocks' offsets
    auto fix_container = [&](std::size_t base, std::uint16_t data_block) {
        add32(out, at(base) + 0x0C, s);
        for (const Sec& sec : section_table(r, base)) {
            if (sec.type == data_block) {
                add32(out, at(sec.entry) + 8, s);
                add32(out, at(sec.offset) + 4, s);
            } else if (after(sec.offset)) {
                add32(out, at(sec.entry) + 4, s);
            }
        }
    };
    // wave archive: this wave's size, the offsets of waves stored after it (empty slots keep -1)
    put32(out, at(entry(wave)) + 8, std::uint32_t(wave_bytes.size()));
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int32_t o = r.s32(entry(i) + 4);
        if (i != wave && o >= 0 && after(wbody + std::size_t(o))) add32(out, at(entry(i)) + 4, s);
    }
    fix_container(c.fwar, 0x6801);
    // group file: the archive's item size, later items' offsets
    if (c.group) {
        const auto gs = sections(r, c.group);
        const std::size_t gbody = gs.at(0x7801) + 8;
        for (std::size_t e : table(r, gs.at(0x7800) + 8)) {
            const std::size_t pos = gbody + std::size_t(r.s32(e + 8));
            if (after(pos)) add32(out, at(e) + 8, s);
            else if (pos <= start && start < pos + r.u32(e + 12)) add32(out, at(e) + 12, s);
        }
        fix_container(c.group, 0x7801);
    }
    // sound archive: file infos (files stored after the cut move; files containing it grow), header, FILE block
    for (std::size_t o : table(r, l.refs.at(0x2106))) {
        if (r.u16(o) != 0x220C) continue;
        const std::size_t ro = o + std::size_t(r.s32(o + 4));
        const std::int32_t off = r.s32(ro + 4);
        if (off < 0) continue;
        const std::size_t pos = l.file_block + 8 + std::size_t(off);
        if (after(pos)) add32(out, ro + 4, s);
        else if (pos <= start && start < pos + r.u32(ro + 8)) add32(out, ro + 8, s);
    }
    fix_container(0, 0x2002);
    return out;
}

// Every stored copy of file `fid`: directly in FILE and inside group files.
std::vector<std::size_t> file_copies(const R& r, std::uint32_t fid) {
    const Layout l = layout(r);
    const std::vector<std::size_t> infos = table(r, l.refs.at(0x2106));
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const std::size_t o = infos[i];
        if (r.u16(o) != 0x220C) continue;
        const std::size_t ro = o + std::size_t(r.s32(o + 4));
        if (r.s32(ro + 4) < 0) continue;
        const std::size_t pos = l.file_block + 8 + std::size_t(r.s32(ro + 4));
        if (i == fid) v.push_back(pos);
        if (r.magic(pos) == "FGRP") {
            const auto gs = sections(r, pos);
            for (std::size_t e : table(r, gs.at(0x7800) + 8))
                if (r.u32(e) == fid) v.push_back(gs.at(0x7801) + 8 + std::size_t(r.s32(e + 8)));
        }
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// Defined below: put a wave (made by `make` from the old wave at `tpl`) into every copy of the wave archive.
Bytes put_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave, const std::function<Bytes(const R&, std::size_t)>& make);

// First stored copy of file `fid`: directly in FILE, or inside a group file.
std::size_t stored_file(const R& r, std::uint32_t fid) {
    const Layout l = layout(r);
    const std::vector<std::size_t> infos = table(r, l.refs.at(0x2106));
    if (fid < infos.size() && r.u16(infos[fid]) == 0x220C) {
        const std::size_t ro = infos[fid] + std::size_t(r.s32(infos[fid] + 4));
        if (r.s32(ro + 4) >= 0) return l.file_block + 8 + std::size_t(r.s32(ro + 4));
    }
    for (std::size_t o : infos) {
        if (r.u16(o) != 0x220C) continue;
        const std::size_t ro = o + std::size_t(r.s32(o + 4));
        if (r.s32(ro + 4) < 0) continue;
        const std::size_t pos = l.file_block + 8 + std::size_t(r.s32(ro + 4));
        if (r.magic(pos) != "FGRP") continue;
        const auto gs = sections(r, pos);
        for (std::size_t e : table(r, gs.at(0x7800) + 8))
            if (r.u32(e) == fid) return gs.at(0x7801) + 8 + std::size_t(r.s32(e + 8));
    }
    throw FormatError("file " + std::to_string(fid) + " is not stored in this archive");
}

}  // namespace

std::vector<std::uint32_t> bank_waves(ByteView data, std::uint32_t bank, std::uint32_t wave_archive) {
    const Archive a = read(data);
    if (bank >= a.banks.size()) throw FormatError("no bank " + std::to_string(bank));
    const R r{data};
    const std::size_t p = stored_file(r, a.banks[bank].file);
    if (r.magic(p) != "FBNK") throw FormatError("bank file is not an FBNK");
    const std::size_t body = sections(r, p).at(0x5800) + 8;
    std::size_t ids = 0;
    for (int k = 0; k < 2; ++k)
        if (r.u16(body + 8 * std::size_t(k)) == 0x0100) ids = body + 8 * std::size_t(k) + std::size_t(r.s32(body + 8 * std::size_t(k) + 4));
    if (!ids) throw FormatError("bank has no wave id table");
    std::vector<std::uint32_t> v;
    for (std::uint32_t i = 0, n = r.u32(ids); i < n; ++i)
        if ((r.u32(ids + 4 + 8 * i) & 0xFFFFFF) == wave_archive) v.push_back(r.u32(ids + 8 + 8 * i));
    return v;
}

Bytes silence_bank(ByteView data, std::uint32_t bank, std::uint32_t wave_archive, std::size_t* count) {
    const Archive a = read(data);
    std::vector<std::uint32_t> mine = bank_waves(data, bank, wave_archive), others;
    for (std::uint32_t b = 0; b < a.banks.size(); ++b) {
        if (b == bank) continue;
        try {
            const auto w = bank_waves(data, b, wave_archive);
            others.insert(others.end(), w.begin(), w.end());
        } catch (const FormatError&) {}  // a bank not stored in this archive uses nothing here
    }
    std::sort(mine.begin(), mine.end());
    mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
    Bytes out(data.begin(), data.end());
    std::size_t n = 0;
    const std::vector<std::vector<std::int16_t>> silence(1, std::vector<std::int16_t>(14, 0));  // one ADPCM frame
    for (std::uint32_t w : mine) {
        if (std::find(others.begin(), others.end(), w) != others.end()) continue;
        out = replace_wave(out, wave_archive, w, 32000, silence, true);
        ++n;
    }
    if (count) *count = n;
    return out;
}

Bytes replace_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave, std::uint32_t sample_rate,
                   const std::vector<std::vector<std::int16_t>>& channels, bool adpcm, std::uint32_t channels_out) {
    return put_wave(data, wave_archive, wave,
                    [&](const R& r, std::size_t tpl) { return build_wave(r, tpl, sample_rate, channels, adpcm, channels_out); });
}

Bytes copy_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave, ByteView from, std::uint32_t from_wave) {
    const Archive fa = read(from);
    if (wave_archive >= fa.wave_archives.size()) throw FormatError("source has no wave archive " + std::to_string(wave_archive));
    const R fr{from};
    const auto fc = warc_copies(fr, fa.wave_archives[wave_archive].file);
    if (fc.empty()) throw FormatError("source does not store wave archive " + std::to_string(wave_archive));
    const auto fs = sections(fr, fc.front().fwar);
    if (from_wave >= fr.u32(fs.at(0x6800) + 8)) throw FormatError("source wave " + std::to_string(from_wave) + " out of range");
    const std::size_t e = fs.at(0x6800) + 12 + 12 * std::size_t(from_wave);
    const std::size_t p = fs.at(0x6801) + 8 + std::size_t(fr.s32(e + 4));
    const Bytes bytes(from.begin() + std::ptrdiff_t(p), from.begin() + std::ptrdiff_t(p + fr.u32(e + 8)));
    if (fr.magic(p) != "FWAV") throw FormatError("source wave is not a BFWAV");
    return put_wave(data, wave_archive, wave, [&](const R&, std::size_t) { return bytes; });
}

Bytes stream_music(ByteView data, std::uint8_t tv_volume, std::uint8_t drc_volume, const std::string& path) {
    // 2/2/8 stream limits crash when a folder is closed: the Menu restarts both music sounds there and the old and new
    // streams overlap, so 4/4/16
    Bytes sar = add_stream(data, "BGM_MEN_TV", "BGM_SET_ACS", path,
                           {"BGM_MEN_DRC_RYTHM", "BGM_MEN_DRC_SYNTH", "BGM_MEN_DRC_SYNTH_2", "BGM_MEN_TV_SYNTH"});
    sar = alias_to_stream(sar, "BGM_MEN_DRC_MAIN", "BGM_MEN_TV");
    sar = set_stream_limits(sar, 4, 4, 16);
    sar = set_sound_volume(sar, "BGM_MEN_TV", tv_volume);
    sar = set_sound_volume(sar, "BGM_MEN_DRC_MAIN", drc_volume);
    const Archive a = read(sar);
    const auto bgm = std::find_if(a.banks.begin(), a.banks.end(), [](const Named& b) { return b.name == "BANK_MEN_BGM"; });
    if (bgm == a.banks.end()) throw FormatError("no bank named BANK_MEN_BGM");
    return silence_bank(sar, bgm->index, 1, nullptr);
}

Wave read_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave) {
    const Archive a = read(data);
    if (wave_archive >= a.wave_archives.size()) throw FormatError("no wave archive " + std::to_string(wave_archive));
    const R r{data};
    const auto copies = warc_copies(r, a.wave_archives[wave_archive].file);
    if (copies.empty()) throw FormatError("wave archive " + std::to_string(wave_archive) + " is not stored in this archive");
    const auto ws = sections(r, copies.front().fwar);
    const std::size_t winfo = ws.at(0x6800), wbody = ws.at(0x6801) + 8;
    if (wave >= r.u32(winfo + 8)) throw FormatError("wave " + std::to_string(wave) + " out of range");
    const std::int32_t at = r.s32(winfo + 12 + 12 * std::size_t(wave) + 4);
    if (at < 0) throw FormatError("wave " + std::to_string(wave) + " is an empty slot");
    const std::size_t p = wbody + std::size_t(at);
    if (r.magic(p) != "FWAV") throw FormatError("wave " + std::to_string(wave) + " is not a BFWAV");
    // laid out as build_wave writes it (and the Menu's own waves are): INFO body = encoding, loop, rate, loop start,
    // sample count, channel table (refs relative to the table), channel infos (sample data relative to the DATA body,
    // DSP-ADPCM info relative to the channel info)
    const auto fs = sections(r, p);
    if (!fs.count(0x7000) || !fs.count(0x7001)) throw FormatError("wave " + std::to_string(wave) + " has no INFO or DATA block");
    const std::size_t b = fs.at(0x7000) + 8, dbody = fs.at(0x7001) + 8;
    Wave w;
    w.encoding = r.u8(b);
    w.loop = r.u8(b + 1) != 0;
    w.sample_rate = r.u32(b + 4);
    w.loop_start = r.u32(b + 8);
    w.samples = r.u32(b + 0x0C);
    const std::size_t table = b + 0x14;
    w.channels = r.u32(table);
    if (!w.channels || w.channels > 8) throw FormatError("wave " + std::to_string(wave) + " has " + std::to_string(w.channels) + " channels");
    if (w.encoding != 2) return w;
    for (std::uint32_t c = 0; c < w.channels; ++c) {
        const std::size_t ci = table + std::size_t(r.s32(table + 8 + 8 * std::size_t(c)));
        const std::size_t ai = ci + std::size_t(r.s32(ci + 0x0C));
        bfstm::AdpcmChannel ch;
        for (int k = 0; k < 16; ++k) ch.coefs[k] = std::int16_t(r.u16(ai + 2 * std::size_t(k)));
        ch.pred_scale = r.u16(ai + 0x20);
        ch.hist1 = std::int16_t(r.u16(ai + 0x22));
        ch.hist2 = std::int16_t(r.u16(ai + 0x24));
        ch.loop_pred_scale = r.u16(ai + 0x26);
        ch.loop_hist1 = std::int16_t(r.u16(ai + 0x28));
        ch.loop_hist2 = std::int16_t(r.u16(ai + 0x2A));
        const std::size_t start = dbody + std::size_t(r.s32(ci + 4)), size = (std::size_t(w.samples) + 13) / 14 * 8;
        if (start + size > data.size()) throw FormatError("wave " + std::to_string(wave) + " data runs past the archive");
        ch.data.assign(data.begin() + std::ptrdiff_t(start), data.begin() + std::ptrdiff_t(start + size));
        w.adpcm.push_back(std::move(ch));
    }
    return w;
}

Bytes set_bank_wave(ByteView data, std::uint32_t bank, std::uint32_t index, std::uint32_t wave, std::uint32_t wave_archive) {
    const Archive a = read(data);
    if (bank >= a.banks.size()) throw FormatError("no bank " + std::to_string(bank));
    const R r{data};
    const std::vector<std::size_t> copies = file_copies(r, a.banks[bank].file);
    if (copies.empty()) throw FormatError("bank file is not stored in this archive");
    Bytes out(data.begin(), data.end());
    std::size_t changed = 0;
    for (std::size_t p : copies) {
        // a bank stored in several groups has a different wave table in each copy (each group's own wave archive):
        // only the copies whose entry plays `wave_archive` are changed
        if (r.magic(p) != "FBNK") throw FormatError("bank file is not an FBNK");
        const std::size_t body = sections(r, p).at(0x5800) + 8;
        std::size_t ids = 0;
        for (int k = 0; k < 2; ++k)
            if (r.u16(body + 8 * std::size_t(k)) == 0x0100) ids = body + 8 * std::size_t(k) + std::size_t(r.s32(body + 8 * std::size_t(k) + 4));
        if (!ids || index >= r.u32(ids)) continue;
        if ((r.u32(ids + 4 + 8 * std::size_t(index)) & 0xFFFFFF) != wave_archive) continue;
        put32(out, ids + 8 + 8 * std::size_t(index), wave);
        if (read_u32(out, ids + 8 + 8 * std::size_t(index), true) != wave) throw FormatError("internal error: bank entry did not read back");
        ++changed;
    }
    if (!changed) throw FormatError("no copy of the bank has entry " + std::to_string(index) + " in wave archive " + std::to_string(wave_archive));
    // nothing else changed: the archive still reads, with the same tables
    const Archive c = read(out);
    if (c.files.size() != a.files.size() || c.banks.size() != a.banks.size()) throw FormatError("internal error: archive tables changed");
    return out;
}

Bytes set_note_velocity(ByteView data, std::uint32_t file, std::uint32_t offset, std::uint8_t velocity,
                        const std::vector<std::string>& groups, std::size_t* count) {
    const Archive a = read(data);
    if (file >= a.files.size()) throw FormatError("no file " + std::to_string(file));
    if (velocity > 127) throw FormatError("velocity must be 0-127");
    const R r{data};
    const Layout l = layout(r);
    // the copies to change: directly in FILE, and inside the named groups (all groups when none are named)
    std::vector<std::size_t> copies;
    if (a.files[file].offset) copies.push_back(*a.files[file].offset);
    for (const auto& g : groups)
        if (std::none_of(a.groups.begin(), a.groups.end(), [&](const Named& n) { return n.name == g; }))
            throw FormatError("no group named " + g);
    const std::vector<std::size_t> infos = table(r, l.refs.at(0x2106));
    for (const Named& g : a.groups) {
        if (!groups.empty() && std::find(groups.begin(), groups.end(), g.name) == groups.end()) continue;
        const File& gf = a.files.at(g.file);
        if (!gf.offset || gf.magic != "FGRP") continue;
        const auto gs = sections(r, *gf.offset);
        for (std::size_t e : table(r, gs.at(0x7800) + 8))
            if (r.u32(e) == file) copies.push_back(gs.at(0x7801) + 8 + std::size_t(r.s32(e + 8)));
    }
    std::sort(copies.begin(), copies.end());
    copies.erase(std::unique(copies.begin(), copies.end()), copies.end());
    if (copies.empty()) throw FormatError("file " + std::to_string(file) + " is not stored in the chosen places");

    Bytes out(data.begin(), data.end());
    std::size_t at_first = 0;
    for (std::size_t p : copies) {
        if (r.magic(p) != "FSEQ") throw FormatError("file " + std::to_string(file) + " is not a sequence (FSEQ)");
        const auto ss = sections(r, p);
        if (!ss.count(0x5000)) throw FormatError("sequence has no DATA block");
        const std::size_t d = ss.at(0x5000) + 8;
        const std::size_t at = d + offset;
        if (at + 2 >= p + r.u32(p + 0x0C)) throw FormatError("offset is outside the sequence data");
        if (r.u8(at) >= 0x80) throw FormatError("no note command at that offset");
        if (!at_first) at_first = at;
        else if (r.u8(at) != r.u8(at_first) || r.u8(at + 1) != r.u8(at_first + 1))
            throw FormatError("the copies of the sequence differ at that offset");
        out[at + 1] = velocity;  // note command: key, velocity, length
    }
    // nothing else changed: same size, the archive still reads with the same tables
    std::size_t diff = 0;
    for (std::size_t i = 0; i < out.size(); ++i) if (out[i] != data[i]) ++diff;
    if (diff > copies.size()) throw FormatError("internal error: more bytes changed than notes");
    const Archive c = read(out);
    if (c.files.size() != a.files.size() || c.sounds.size() != a.sounds.size()) throw FormatError("internal error: archive tables changed");
    if (count) *count = copies.size();
    return out;
}

Bytes share_bank_table(ByteView data, std::uint32_t bank, std::uint32_t wave_archive, std::size_t* count) {
    const Archive a = read(data);
    if (bank >= a.banks.size()) throw FormatError("no bank " + std::to_string(bank));
    const R r{data};
    auto table_of = [&](std::size_t p) -> std::size_t {
        if (r.magic(p) != "FBNK") throw FormatError("bank file is not an FBNK");
        const std::size_t body = sections(r, p).at(0x5800) + 8;
        for (int k = 0; k < 2; ++k)
            if (r.u16(body + 8 * std::size_t(k)) == 0x0100) return body + 8 * std::size_t(k) + std::size_t(r.s32(body + 8 * std::size_t(k) + 4));
        throw FormatError("bank has no wave id table");
    };
    const std::vector<std::size_t> copies = file_copies(r, a.banks[bank].file);
    std::size_t src = 0;
    for (std::size_t p : copies) {
        const std::size_t t = table_of(p);
        if (r.u32(t) && (r.u32(t + 4) & 0xFFFFFF) == wave_archive) { src = t; break; }
    }
    if (!src) throw FormatError("no copy of the bank plays wave archive " + std::to_string(wave_archive));
    const std::uint32_t n = r.u32(src);
    Bytes out(data.begin(), data.end());
    std::size_t changed = 0;
    for (std::size_t p : copies) {
        const std::size_t t = table_of(p);
        if (t == src || r.u32(t) != n) continue;   // only copies with the same entry count
        std::copy(data.begin() + std::ptrdiff_t(src + 4), data.begin() + std::ptrdiff_t(src + 4 + 8 * std::size_t(n)), out.begin() + std::ptrdiff_t(t + 4));
        ++changed;
    }
    const Archive c = read(out);
    if (c.files.size() != a.files.size() || c.banks.size() != a.banks.size()) throw FormatError("internal error: archive tables changed");
    if (count) *count = changed;
    return out;
}

namespace {   // same unnamed namespace as its declaration above
Bytes put_wave(ByteView data, std::uint32_t wave_archive, std::uint32_t wave, const std::function<Bytes(const R&, std::size_t)>& make) {
    const Archive a = read(data);
    if (wave_archive >= a.wave_archives.size()) throw FormatError("no wave archive " + std::to_string(wave_archive));
    const std::uint32_t fid = a.wave_archives[wave_archive].file;
    const std::size_t copies = warc_copies(R{data}, fid).size();
    if (!copies) throw FormatError("wave archive " + std::to_string(wave_archive) + " (file " + std::to_string(fid) + ") is not stored in this archive");
    Bytes out(data.begin(), data.end());
    Bytes wave_bytes;
    for (std::size_t k = 0; k < copies; ++k) {
        const R r{out};
        const WarcCopy c = warc_copies(r, fid).at(k);
        const auto ws = sections(r, c.fwar);
        if (wave >= r.u32(ws.at(0x6800) + 8)) throw FormatError("wave " + std::to_string(wave) + " out of range");
        const std::size_t tpl = ws.at(0x6801) + 8 + std::size_t(r.s32(ws.at(0x6800) + 12 + 12 * std::size_t(wave) + 4));
        if (k == 0) wave_bytes = make(r, tpl);
        out = splice_wave(out, c, wave, wave_bytes);
    }

    // verify: same files and groups; in every copy the new wave is in place and all other waves are unchanged;
    // every other stored file is byte-identical
    const Archive check = read(out);
    if (check.files.size() != a.files.size() || check.sounds.size() != a.sounds.size() || check.groups.size() != a.groups.size())
        throw FormatError("internal error: archive tables changed");
    const R ro{data}, rn{out};
    const auto oc = warc_copies(ro, fid), nc = warc_copies(rn, fid);
    if (oc.size() != nc.size()) throw FormatError("internal error: wave archive copies changed");
    auto wave_at = [](const R& r, std::size_t fwar, std::uint32_t i) {
        const auto ws = sections(r, fwar);
        const std::size_t e = ws.at(0x6800) + 12 + 12 * std::size_t(i);
        const std::size_t p = ws.at(0x6801) + 8 + std::size_t(r.s32(e + 4));
        return Bytes(r.b.begin() + std::ptrdiff_t(p), r.b.begin() + std::ptrdiff_t(p + r.u32(e + 8)));
    };
    for (std::size_t k = 0; k < oc.size(); ++k) {
        const std::uint32_t n = ro.u32(sections(ro, oc[k].fwar).at(0x6800) + 8);
        if (rn.u32(sections(rn, nc[k].fwar).at(0x6800) + 8) != n) throw FormatError("internal error: wave count changed");
        if (wave_at(rn, nc[k].fwar, wave) != wave_bytes) throw FormatError("internal error: new wave did not read back");
        const std::size_t oe = sections(ro, oc[k].fwar).at(0x6800) + 12, ne = sections(rn, nc[k].fwar).at(0x6800) + 12;
        for (std::uint32_t i = 0; i < n; ++i) {
            if (i != wave && wave_at(ro, oc[k].fwar, i) != wave_at(rn, nc[k].fwar, i))
                throw FormatError("internal error: wave " + std::to_string(i) + " changed");
            if ((ro.s32(oe + 12 * std::size_t(i) + 4) < 0) != (rn.s32(ne + 12 * std::size_t(i) + 4) < 0))
                throw FormatError("internal error: wave " + std::to_string(i) + " changed between empty and stored");
        }
    }
    for (std::size_t i = 0; i < a.files.size(); ++i) {
        const File& f = a.files[i];
        const File& g = check.files[i];
        if (!f.offset || f.magic == "FGRP" || f.magic == "FWAR") continue;
        if (!g.offset || g.size != f.size ||
            !std::equal(data.begin() + std::ptrdiff_t(*f.offset), data.begin() + std::ptrdiff_t(*f.offset + f.size), out.begin() + std::ptrdiff_t(*g.offset)))
            throw FormatError("internal error: file " + std::to_string(i) + " changed");
    }
    for (std::size_t gi = 0; gi < a.group_items.size(); ++gi) {
        const auto& x = a.group_items[gi];
        const auto& y = check.group_items[gi];
        if (x.size() != y.size()) throw FormatError("internal error: group items changed");
        for (std::size_t j = 0; j < x.size(); ++j)
            if (x[j].file != y[j].file || x[j].magic != y[j].magic || x[j].wave_count != y[j].wave_count ||
                (x[j].file != fid && x[j].size != y[j].size))
                throw FormatError("internal error: group item changed");
    }
    return out;
}
}  // namespace

}  // namespace wiiumenu::bfsar
