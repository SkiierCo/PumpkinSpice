#include "wiiumenu/yaz0.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace wiiumenu::yaz0 {

bool is_yaz0(ByteView data) { return has_magic(data, "Yaz0") && data.size() >= 16; }

std::uint32_t header_alignment(ByteView data) { return is_yaz0(data) ? read_u32(data, 8, true) : 0; }

Bytes decompress(ByteView data, std::size_t max_out) {
    if (!is_yaz0(data)) throw FormatError("not Yaz0");
    std::size_t size = read_u32(data, 4, true);
    if (max_out && max_out < size) size = max_out;
    Bytes out;
    out.reserve(size);
    std::size_t src = 16;
    while (out.size() < size) {
        if (src >= data.size()) throw FormatError("Yaz0 stream truncated");
        const std::uint8_t code = data[src++];
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            if (code & (1u << bit)) {
                if (src >= data.size()) throw FormatError("Yaz0 stream truncated");
                out.push_back(data[src++]);
                continue;
            }
            if (src + 1 >= data.size()) throw FormatError("Yaz0 stream truncated");
            const std::uint8_t b1 = data[src++], b2 = data[src++];
            const std::size_t dist = (std::size_t(b1 & 0x0F) << 8 | b2) + 1;
            std::size_t len = b1 >> 4;
            if (len == 0) {
                if (src >= data.size()) throw FormatError("Yaz0 stream truncated");
                len = std::size_t(data[src++]) + 0x12;
            } else {
                len += 2;
            }
            if (dist > out.size()) throw FormatError("Yaz0 back-reference out of range");
            const std::size_t from = out.size() - dist;
            for (std::size_t i = 0; i < len && out.size() < size; ++i) out.push_back(out[from + i]);
        }
    }
    return out;
}

Stream::Stream(ByteView data) : src_(data) {
    if (!is_yaz0(data)) throw FormatError("not Yaz0");
    size_ = read_u32(data, 4, true);
}

std::uint8_t Stream::next() {
    if (pos_ >= size_) throw FormatError("read past the end of a Yaz0 stream");
    std::uint8_t b;
    if (copy_left_) {
        b = ring_[(pos_ - copy_dist_) & 0xFFF];
        --copy_left_;
    } else {
        if (bits_ == 0) {
            if (s_ >= src_.size()) throw FormatError("Yaz0 stream truncated");
            code_ = src_[s_++];
            bits_ = 8;
        }
        --bits_;
        if (code_ & (1u << bits_)) {
            if (s_ >= src_.size()) throw FormatError("Yaz0 stream truncated");
            b = src_[s_++];
        } else {
            if (s_ + 1 >= src_.size()) throw FormatError("Yaz0 stream truncated");
            const std::uint8_t b1 = src_[s_++], b2 = src_[s_++];
            const std::size_t dist = (std::size_t(b1 & 0x0F) << 8 | b2) + 1;
            std::size_t len = b1 >> 4;
            if (len == 0) {
                if (s_ >= src_.size()) throw FormatError("Yaz0 stream truncated");
                len = std::size_t(src_[s_++]) + 0x12;
            } else {
                len += 2;
            }
            if (dist > pos_) throw FormatError("Yaz0 back-reference out of range");
            copy_dist_ = dist;
            copy_left_ = len - 1;
            b = ring_[(pos_ - dist) & 0xFFF];
        }
    }
    ring_[pos_ & 0xFFF] = b;
    ++pos_;
    return b;
}

void Stream::read(std::uint8_t* dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) dst[i] = next();
}

void Stream::skip(std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) next();
}

namespace {

constexpr std::size_t kWindow = 0x1000;
constexpr std::size_t kMinMatch = 3;
constexpr std::size_t kMaxMatch = 0x111;
constexpr std::size_t kHashBits = 15;
constexpr int kMaxChain = 256;

// The hash chain only ever follows positions within kWindow of the current one (older ones end the walk before their
// link is read), so `prev` is a ring of 2 * kWindow entries instead of one per input byte: same matches, same output,
// 32 KiB instead of 4 bytes per input byte (92 MB for a 23 MB archive, too much for the console).
constexpr std::size_t kRing = 2 * kWindow;

// The input as it is read: only the bytes the matcher can still reach (kKeep back, up to a match ahead) are held.
constexpr std::size_t kKeep = 2 * kWindow;
constexpr std::size_t kChunk = 0x40000;

class Window {
public:
    Window(std::size_t size, const Reader& read) : size_(size), read_(read) {}
    std::size_t size() const { return size_; }
    std::uint8_t operator[](std::size_t p) const { return buf_[p - base_]; }
    // makes [p - kKeep, upto) available (upto is clamped to the input's size)
    void need(std::size_t p, std::size_t upto) {
        upto = std::min(upto, size_);
        if (p > base_ + kKeep + kChunk) {  // drop what is out of reach, in large steps
            const std::size_t nb = p - kKeep;
            buf_.erase(buf_.begin(), buf_.begin() + std::ptrdiff_t(nb - base_));
            base_ = nb;
        }
        const std::size_t end = base_ + buf_.size();
        if (upto <= end) return;
        const std::size_t n = std::min(std::max(upto - end, kChunk), size_ - end);
        buf_.resize(buf_.size() + n);
        read_(buf_.data() + buf_.size() - n, n);
    }

private:
    std::size_t size_;
    const Reader& read_;
    Bytes buf_;
    std::size_t base_ = 0;
};

struct Matcher {
    const Window& d;
    std::vector<std::int32_t> head, prev;
    explicit Matcher(const Window& data) : d(data), head(std::size_t(1) << kHashBits, -1), prev(kRing, -1) {}

    std::size_t key(std::size_t p) const {
        return ((std::size_t(d[p]) << 10) ^ (std::size_t(d[p + 1]) << 5) ^ d[p + 2]) & ((1u << kHashBits) - 1);
    }
    void insert(std::size_t p) {
        if (p + kMinMatch > d.size()) return;
        const std::size_t k = key(p);
        prev[p % kRing] = head[k];
        head[k] = std::int32_t(p);
    }
    // Longest match for position p among already-inserted positions.
    std::pair<std::size_t, std::size_t> find(std::size_t p) const {
        std::size_t best_len = 0, best_dist = 0;
        if (p + kMinMatch > d.size()) return {0, 0};
        const std::size_t limit = std::min(kMaxMatch, d.size() - p);
        std::int32_t cand = head[key(p)];
        for (int chain = 0; cand >= 0 && chain < kMaxChain; ++chain, cand = prev[std::size_t(cand) % kRing]) {
            const std::size_t c = std::size_t(cand);
            if (p - c > kWindow) break;
            std::size_t l = 0;
            while (l < limit && d[c + l] == d[p + l]) ++l;
            if (l > best_len) {
                best_len = l;
                best_dist = p - c;
                if (l == limit) break;
            }
        }
        return best_len >= kMinMatch ? std::pair{best_len, best_dist} : std::pair<std::size_t, std::size_t>{0, 0};
    }
};

}  // namespace

Bytes compress(ByteView data, std::uint32_t alignment) {
    std::size_t at = 0;
    const Reader read = [&](std::uint8_t* dst, std::size_t n) {
        std::memcpy(dst, data.data() + at, n);
        at += n;
    };
    return compress(data.size(), read, alignment);
}

Bytes compress(std::size_t size, const Reader& read, std::uint32_t alignment) {
    Bytes out{'Y', 'a', 'z', '0'};
    put_u32(out, std::uint32_t(size), true);
    put_u32(out, alignment, true);
    put_u32(out, 0, true);

    Window data(size, read);
    Matcher m(data);
    std::size_t p = 0;
    while (p < data.size()) {
        const std::size_t code_pos = out.size();
        out.push_back(0);
        std::uint8_t code = 0;
        for (int bit = 7; bit >= 0 && p < data.size(); --bit) {
            data.need(p, p + 1 + kMaxMatch + kMinMatch);  // what find(p) and find(p + 1) read
            auto [len, dist] = m.find(p);
            if (len) {
                // Lazy step: prefer a literal now if the next position matches longer.
                m.insert(p);
                auto [nlen, ndist] = m.find(p + 1);
                (void)ndist;
                if (nlen > len + 1) {
                    code |= std::uint8_t(1u << bit);
                    out.push_back(data[p]);
                    ++p;
                    continue;
                }
                const std::size_t dv = dist - 1;
                if (len >= 0x12) {
                    out.push_back(std::uint8_t(dv >> 8));
                    out.push_back(std::uint8_t(dv));
                    out.push_back(std::uint8_t(len - 0x12));
                } else {
                    out.push_back(std::uint8_t((len - 2) << 4 | dv >> 8));
                    out.push_back(std::uint8_t(dv));
                }
                for (std::size_t i = 1; i < len; ++i) m.insert(p + i);
                p += len;
            } else {
                m.insert(p);
                code |= std::uint8_t(1u << bit);
                out.push_back(data[p]);
                ++p;
            }
        }
        out[code_pos] = code;
    }
    return out;
}

}  // namespace wiiumenu::yaz0
