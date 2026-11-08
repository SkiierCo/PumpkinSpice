#include "wiiumenu/bps.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace wiiumenu::bps {
namespace {

const std::array<std::uint32_t, 256>& crc_table() {
    static const std::array<std::uint32_t, 256> t = [] {
        std::array<std::uint32_t, 256> a{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            a[i] = c;
        }
        return a;
    }();
    return t;
}

enum Action : std::uint64_t { SourceRead = 0, TargetRead = 1, SourceCopy = 2, TargetCopy = 3 };

struct Reader {
    ByteView b;
    std::size_t p = 0;
    std::size_t end;
    std::uint8_t byte() {
        if (p >= end) throw FormatError("BPS patch is truncated");
        return b[p++];
    }
    std::uint64_t number() {
        std::uint64_t data = 0, shift = 1;
        for (int i = 0; i < 10; ++i) {
            const std::uint8_t x = byte();
            data += (x & 0x7F) * shift;
            if (x & 0x80) return data;
            shift <<= 7;
            data += shift;
        }
        throw FormatError("BPS number too large");
    }
};

void put_number(Bytes& out, std::uint64_t n) {
    while (true) {
        const std::uint8_t x = std::uint8_t(n & 0x7F);
        n >>= 7;
        if (n == 0) { out.push_back(0x80 | x); return; }
        out.push_back(x);
        --n;
    }
}

void put_u32le(Bytes& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}

std::uint32_t get_u32le(ByteView b, std::size_t o) {
    return std::uint32_t(b[o]) | std::uint32_t(b[o + 1]) << 8 | std::uint32_t(b[o + 2]) << 16 | std::uint32_t(b[o + 3]) << 24;
}

// Hash chains over 4-byte windows, used to find copies in source and target.
constexpr std::size_t kMin = 4;
constexpr std::size_t kHashBits = 22;
constexpr int kMaxChain = 48;

struct Index {
    ByteView d;
    std::vector<std::int32_t> head, prev;
    explicit Index(ByteView data) : d(data), head(std::size_t(1) << kHashBits, -1), prev(data.size(), -1) {}
    static std::size_t key(const std::uint8_t* p) {
        const std::uint32_t v = std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
        return (v * 2654435761u) >> (32 - kHashBits);
    }
    void insert(std::size_t p) {
        if (p + kMin > d.size()) return;
        const std::size_t k = key(d.data() + p);
        prev[p] = head[k];
        head[k] = std::int32_t(p);
    }
};

// Longest match of target[t..] among indexed positions of `idx` (limited to < limit_pos when given).
std::pair<std::size_t, std::size_t> best(const Index& idx, ByteView target, std::size_t t, std::size_t limit_pos) {
    std::size_t best_len = 0, best_pos = 0;
    if (t + kMin > target.size()) return {0, 0};
    std::int32_t c = idx.head[Index::key(target.data() + t)];
    for (int chain = 0; c >= 0 && chain < kMaxChain; ++chain, c = idx.prev[std::size_t(c)]) {
        const std::size_t s = std::size_t(c);
        if (s >= limit_pos) continue;
        std::size_t l = 0;
        const std::size_t max = std::min(idx.d.size() - s, target.size() - t);
        while (l < max && idx.d[s + l] == target[t + l]) ++l;
        if (l > best_len) { best_len = l; best_pos = s; }
    }
    return {best_len, best_pos};
}

}  // namespace

std::uint32_t crc32(ByteView data, std::uint32_t previous) {
    const auto& t = crc_table();
    std::uint32_t c = previous ^ 0xFFFFFFFFu;
    for (auto b : data) c = t[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

Bytes apply(ByteView patch, ByteView source) {
    if (patch.size() < 4 + 3 + 12 || !has_magic(patch, "BPS1")) throw FormatError("not a BPS patch");
    const std::size_t footer = patch.size() - 12;
    if (crc32(patch.subspan(0, patch.size() - 4)) != get_u32le(patch, footer + 8))
        throw FormatError("BPS patch is damaged (patch checksum mismatch)");
    if (crc32(source) != get_u32le(patch, footer))
        throw FormatError("this patch was made for a different source file (source checksum mismatch)");

    Reader r{patch, 4, footer};
    const std::uint64_t source_size = r.number(), target_size = r.number(), meta = r.number();
    if (source_size != source.size()) throw FormatError("source file has the wrong size for this patch");
    if (meta > footer - r.p) throw FormatError("BPS metadata out of range");
    r.p += std::size_t(meta);

    Bytes out;
    out.reserve(std::size_t(target_size));
    std::int64_t src_rel = 0, tgt_rel = 0;
    while (r.p < footer) {
        const std::uint64_t data = r.number();
        const std::uint64_t cmd = data & 3, len = (data >> 2) + 1;
        if (out.size() + len > target_size) throw FormatError("BPS action writes past the target size");
        switch (cmd) {
            case SourceRead:
                if (out.size() + len > source.size()) throw FormatError("BPS source read out of range");
                out.insert(out.end(), source.begin() + std::ptrdiff_t(out.size()), source.begin() + std::ptrdiff_t(out.size() + len));
                break;
            case TargetRead:
                for (std::uint64_t i = 0; i < len; ++i) out.push_back(r.byte());
                break;
            case SourceCopy: {
                const std::uint64_t d = r.number();
                src_rel += (d & 1) ? -std::int64_t(d >> 1) : std::int64_t(d >> 1);
                if (src_rel < 0 || std::uint64_t(src_rel) + len > source.size()) throw FormatError("BPS source copy out of range");
                out.insert(out.end(), source.begin() + src_rel, source.begin() + src_rel + std::int64_t(len));
                src_rel += std::int64_t(len);
                break;
            }
            case TargetCopy: {
                const std::uint64_t d = r.number();
                tgt_rel += (d & 1) ? -std::int64_t(d >> 1) : std::int64_t(d >> 1);
                if (tgt_rel < 0 || std::uint64_t(tgt_rel) >= out.size()) throw FormatError("BPS target copy out of range");
                for (std::uint64_t i = 0; i < len; ++i) out.push_back(out[std::size_t(tgt_rel++)]);  // may overlap
                break;
            }
        }
    }
    if (out.size() != target_size) throw FormatError("BPS result has the wrong size");
    if (crc32(out) != get_u32le(patch, footer + 4)) throw FormatError("BPS result does not match the target checksum");
    return out;
}

Bytes create(ByteView source, ByteView target, const std::string& metadata) {
    Bytes out{'B', 'P', 'S', '1'};
    put_number(out, source.size());
    put_number(out, target.size());
    put_number(out, metadata.size());
    out.insert(out.end(), metadata.begin(), metadata.end());

    Index src(source), tgt(target);
    for (std::size_t i = 0; i + kMin <= source.size(); ++i) src.insert(i);

    std::size_t t = 0, literal_start = 0, tgt_indexed = 0;
    std::int64_t src_rel = 0, tgt_rel = 0;
    auto flush_literal = [&](std::size_t upto) {
        if (upto == literal_start) return;
        put_number(out, (std::uint64_t(upto - literal_start - 1) << 2) | TargetRead);
        out.insert(out.end(), target.begin() + std::ptrdiff_t(literal_start), target.begin() + std::ptrdiff_t(upto));
    };
    auto put_offset = [&](std::int64_t delta) {
        put_number(out, (std::uint64_t(delta < 0 ? -delta : delta) << 1) | (delta < 0 ? 1u : 0u));
    };

    while (t < target.size()) {
        // Same offset in the source: cheapest action (no offset to store).
        std::size_t read_len = 0;
        while (t + read_len < target.size() && t + read_len < source.size() && source[t + read_len] == target[t + read_len]) ++read_len;
        while (tgt_indexed < t) tgt.insert(tgt_indexed++);
        auto [scopy_len, scopy_pos] = best(src, target, t, source.size());
        auto [tcopy_len, tcopy_pos] = best(tgt, target, t, t);

        std::uint64_t action = TargetRead;
        std::size_t len = 0;
        if (read_len >= kMin && read_len + 4 >= std::max(scopy_len, tcopy_len)) { action = SourceRead; len = read_len; }
        else if (scopy_len >= kMin && scopy_len >= tcopy_len) { action = SourceCopy; len = scopy_len; }
        else if (tcopy_len >= kMin) { action = TargetCopy; len = tcopy_len; }

        if (action == TargetRead) { ++t; continue; }
        flush_literal(t);
        put_number(out, (std::uint64_t(len - 1) << 2) | action);
        if (action == SourceCopy) {
            put_offset(std::int64_t(scopy_pos) - src_rel);
            src_rel = std::int64_t(scopy_pos + len);
        } else if (action == TargetCopy) {
            put_offset(std::int64_t(tcopy_pos) - tgt_rel);
            tgt_rel = std::int64_t(tcopy_pos + len);
        }
        t += len;
        literal_start = t;
    }
    flush_literal(t);

    put_u32le(out, crc32(source));
    put_u32le(out, crc32(target));
    put_u32le(out, crc32(out));

    const Bytes check = bps::apply(out, source);  // never hand out a patch that does not reproduce the target
    if (check.size() != target.size() || !std::equal(check.begin(), check.end(), target.begin()))
        throw FormatError("internal error: created BPS patch does not reproduce the target");
    return out;
}

}  // namespace wiiumenu::bps
