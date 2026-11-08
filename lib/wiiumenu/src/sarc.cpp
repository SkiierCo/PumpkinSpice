#include "wiiumenu/sarc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

namespace wiiumenu::sarc {
namespace {

std::uint32_t pow2_alignment(std::size_t offset, std::uint32_t cap = 0x10000) {
    std::uint32_t a = 4;
    while (a < cap && offset % (std::size_t(a) * 2) == 0) a *= 2;
    return a;
}

// Largest absolute alignment observed per extension in stock 5.5.5 EU files.
struct ExtRule { std::string_view ext; std::uint32_t align; };
constexpr ExtRule kExtAlignment[] = {
    {".bflim", 0x2000}, {".bfres", 0x2000}, {".bffnt", 0x2000}, {".tga", 0x2000},
    {".byml", 0x2000},  {".genvb", 0x2000}, {".genvres", 0x2000}, {".bofx", 0x2000},
    {".ptcl", 0x4000},  {".baglmf", 0x1000}, {".btsnd", 0x1000}, {".dsp", 0x1000},
    {".szs", 0x2000},   {".sarc", 0x2000},  {".arc", 0x100},     {".msbt", 0x100},
    {".gsh", 0x100},    {".gtx", 0x2000},   {".ffsd", 0x100},    {".sharcfb", 0x100},
};

}  // namespace

std::uint32_t hash(std::string_view name, std::uint32_t mult) {
    std::uint32_t h = 0;
    for (char c : name) h = h * mult + std::uint32_t(std::int32_t(static_cast<signed char>(c)));
    return h;
}

std::uint32_t default_alignment(std::string_view name, ByteView data) {
    std::uint32_t a = 0x100;
    auto dot = name.rfind('.');
    if (dot != std::string_view::npos) {
        std::string ext(name.substr(dot));
        for (auto& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        for (const auto& r : kExtAlignment)
            if (r.ext == ext) { a = r.align; break; }
    }
    if (has_magic(data, "Yaz0") && data.size() >= 12) a = std::max(a, read_u32(data, 8, true));
    // A texture's image data starts at the file's first byte and must sit at the alignment in
    // its own header: GX2 derives pipe/bank swizzle from address bits.
    if (data.size() >= 0x28 && has_magic(data.subspan(data.size() - 0x28), "FLIM"))
        a = std::max<std::uint32_t>(a, read_u16(data, data.size() - 0x28 + 0x20, true));
    // A nested archive is used in place: its members are only aligned in absolute terms if the
    // archive itself starts at the largest alignment any member needs.
    if (has_magic(data, "SARC")) {
        try {
            for (const auto& f : ArchiveView::parse(data).files)
                a = std::max(a, default_alignment(f.name, f.data));
        } catch (const FormatError&) {
        }
    }
    return std::max<std::uint32_t>(a, 4);
}

Archive Archive::parse(ByteView raw) {
    const ArchiveView v = ArchiveView::parse(raw);
    Archive s;
    s.hash_mult = v.hash_mult;
    s.big_endian = v.big_endian;
    s.version = v.version;
    s.tail_padding = v.tail_padding;
    s.files.reserve(v.files.size());
    for (const FileView& f : v.files) s.files.push_back({f.name, Bytes(f.data.begin(), f.data.end()), f.alignment});
    return s;
}

ArchiveView ArchiveView::parse(ByteView raw) { return parse_index(raw, raw.size(), true); }

ArchiveView ArchiveView::parse_index(ByteView head, std::size_t archive_size) {
    return parse_index(head, archive_size, false);
}

ArchiveView ArchiveView::parse_index(ByteView raw, std::size_t archive_size, bool with_data) {
    if (!has_magic(raw, "SARC")) throw FormatError("not a SARC archive");
    const bool be = read_u16(raw, 6, true) == 0xFEFF;
    const std::uint16_t hlen = read_u16(raw, 4, be);
    const std::uint32_t fsize = read_u32(raw, 8, be);
    const std::uint32_t doff = read_u32(raw, 12, be);
    ArchiveView s;
    s.big_endian = be;
    s.version = read_u16(raw, 16, be);

    std::size_t o = hlen;
    if (!has_magic(raw.subspan(o), "SFAT")) throw FormatError("missing SFAT");
    const std::uint16_t count = read_u16(raw, o + 6, be);
    s.hash_mult = read_u32(raw, o + 8, be);
    const std::size_t ents = o + 12;
    o = ents + std::size_t(count) * 16;
    if (!has_magic(raw.subspan(o), "SFNT")) throw FormatError("missing SFNT");
    const std::size_t names_base = o + 8;

    std::uint32_t last_end = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t e = ents + i * 16;
        const std::uint32_t h = read_u32(raw, e, be), attr = read_u32(raw, e + 4, be);
        const std::uint32_t start = read_u32(raw, e + 8, be), end = read_u32(raw, e + 12, be);
        if (std::size_t(doff) + end > archive_size || start > end) throw FormatError("file entry out of range");
        FileView f;
        if (attr & 0x01000000) {
            const std::size_t no = names_base + std::size_t(attr & 0xFFFF) * 4;
            std::size_t z = no;
            while (z < raw.size() && raw[z] != 0) ++z;
            f.name.assign(reinterpret_cast<const char*>(raw.data() + no), z - no);
        } else {
            char buf[32];
            std::snprintf(buf, sizeof buf, "_unnamed/%08x", h);
            f.name = buf;
        }
        f.offset = std::size_t(doff) + start;
        f.size = end - start;
        if (with_data) {
            f.data = raw.subspan(f.offset, f.size);
        } else {
            f.indexed = true;
        }
        f.alignment = pow2_alignment(std::size_t(doff) + start);
        last_end = std::max(last_end, end);
        s.files.push_back(std::move(f));
    }
    const std::size_t used = std::size_t(doff) + last_end;
    s.tail_padding = fsize > used ? std::uint32_t(fsize - used) : 0;
    return s;
}

const File* Archive::get(std::string_view name) const {
    for (const auto& f : files)
        if (f.name == name) return &f;
    return nullptr;
}

Bytes Archive::write() const {
    ArchiveView v;
    v.hash_mult = hash_mult;
    v.big_endian = big_endian;
    v.version = version;
    v.tail_padding = tail_padding;
    v.files.reserve(files.size());
    for (const File& f : files) v.files.push_back({f.name, ByteView(f.data), f.alignment});
    return layout(v).join();
}

Layout layout(const ArchiveView& archive) {
    const bool be = archive.big_endian;
    const std::uint32_t hash_mult = archive.hash_mult;
    std::vector<const FileView*> sorted;
    for (const auto& f : archive.files) sorted.push_back(&f);
    std::stable_sort(sorted.begin(), sorted.end(), [&](const FileView* a, const FileView* b) {
        return hash(a->name, hash_mult) < hash(b->name, hash_mult);
    });

    Bytes names;
    std::vector<std::uint32_t> name_off, aligns;
    for (const FileView* f : sorted) {
        name_off.push_back(std::uint32_t(names.size() / 4));
        names.insert(names.end(), f->name.begin(), f->name.end());
        names.push_back(0);
        while (names.size() % 4) names.push_back(0);
        aligns.push_back(f->alignment ? f->alignment : default_alignment(f->name, f->data));
    }

    const std::size_t header_end = 0x14 + 12 + 16 * sorted.size() + 8 + names.size();
    const std::size_t doff = align_up(header_end, sorted.empty() ? 4 : aligns[0]);

    Layout l;
    Bytes sfat;
    std::size_t body = 0;  // bytes after doff so far
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const std::size_t pos = doff + body;
        const std::size_t zeros = align_up(pos, aligns[i]) - pos;
        const std::uint32_t start = std::uint32_t(body + zeros);
        const FileView& f = *sorted[i];
        body = start + (f.indexed ? f.size : f.data.size());
        l.pieces.push_back({zeros, f.data, f.indexed, f.offset, f.size});
        put_u32(sfat, hash(sorted[i]->name, hash_mult), be);
        put_u32(sfat, 0x01000000 | name_off[i], be);
        put_u32(sfat, start, be);
        put_u32(sfat, std::uint32_t(body), be);
    }
    l.tail = archive.tail_padding;
    body += l.tail;

    Bytes& out = l.head;
    out.reserve(doff);
    out.insert(out.end(), {'S', 'A', 'R', 'C'});
    put_u16(out, 0x14, be);
    put_u16(out, 0xFEFF, be);
    put_u32(out, std::uint32_t(doff + body), be);
    put_u32(out, std::uint32_t(doff), be);
    put_u16(out, archive.version, be);
    put_u16(out, 0, be);
    out.insert(out.end(), {'S', 'F', 'A', 'T'});
    put_u16(out, 0xC, be);
    put_u16(out, std::uint16_t(sorted.size()), be);
    put_u32(out, hash_mult, be);
    out.insert(out.end(), sfat.begin(), sfat.end());
    out.insert(out.end(), {'S', 'F', 'N', 'T'});
    put_u16(out, 8, be);
    put_u16(out, 0, be);
    out.insert(out.end(), names.begin(), names.end());
    out.resize(doff, 0);
    l.size = doff + body;
    return l;
}

std::vector<Layout::Segment> Layout::segments() const {
    std::vector<Segment> s;
    s.reserve(2 * pieces.size() + 2);
    s.push_back({head.data(), head.size(), false, 0});
    for (const Piece& p : pieces) {
        if (p.zeros) s.push_back({nullptr, p.zeros, false, 0});
        if (p.indexed) {
            if (p.size) s.push_back({nullptr, p.size, true, p.offset});
        } else if (!p.data.empty()) {
            s.push_back({p.data.data(), p.data.size(), false, 0});
        }
    }
    if (tail) s.push_back({nullptr, tail, false, 0});
    return s;
}

Bytes Layout::join() const {
    Bytes out;
    out.reserve(size);  // one block of exactly the archive's size
    for (const Segment& s : segments()) {
        if (s.indexed) throw FormatError("archive layout refers to a file that was not read");
        if (s.data) {
            out.insert(out.end(), s.data, s.data + s.size);
        } else {
            out.resize(out.size() + s.size, 0);
        }
    }
    return out;
}

}  // namespace wiiumenu::sarc
