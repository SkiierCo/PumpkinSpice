// Endian-aware byte helpers shared by every format module.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace wiiumenu {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline std::uint16_t read_u16(ByteView b, std::size_t o, bool be) {
    if (o + 2 > b.size()) throw FormatError("read past end");
    return be ? std::uint16_t(b[o] << 8 | b[o + 1]) : std::uint16_t(b[o + 1] << 8 | b[o]);
}

inline std::uint32_t read_u32(ByteView b, std::size_t o, bool be) {
    if (o + 4 > b.size()) throw FormatError("read past end");
    return be ? (std::uint32_t(b[o]) << 24 | std::uint32_t(b[o + 1]) << 16 | std::uint32_t(b[o + 2]) << 8 | b[o + 3])
              : (std::uint32_t(b[o + 3]) << 24 | std::uint32_t(b[o + 2]) << 16 | std::uint32_t(b[o + 1]) << 8 | b[o]);
}

inline void put_u16(Bytes& out, std::uint16_t v, bool be) {
    if (be) { out.push_back(std::uint8_t(v >> 8)); out.push_back(std::uint8_t(v)); }
    else    { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); }
}

inline void put_u32(Bytes& out, std::uint32_t v, bool be) {
    for (int i = 0; i < 4; ++i) {
        int shift = be ? 24 - 8 * i : 8 * i;
        out.push_back(std::uint8_t(v >> shift));
    }
}

inline bool has_magic(ByteView b, const char (&m)[5]) {
    return b.size() >= 4 && b[0] == std::uint8_t(m[0]) && b[1] == std::uint8_t(m[1]) &&
           b[2] == std::uint8_t(m[2]) && b[3] == std::uint8_t(m[3]);
}

inline std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) / a * a; }

}  // namespace wiiumenu
