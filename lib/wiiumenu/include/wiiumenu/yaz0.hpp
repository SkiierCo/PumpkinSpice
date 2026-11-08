// Yaz0 (Nintendo LZ77 variant). Header: "Yaz0", u32 BE decompressed size,
// u32 BE data alignment, u32 reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "wiiumenu/bytes.hpp"

namespace wiiumenu::yaz0 {

bool is_yaz0(ByteView data);
std::uint32_t header_alignment(ByteView data);

// Decompress; if max_out is non-zero, stop after that many output bytes.
Bytes decompress(ByteView data, std::size_t max_out = 0);

// Decompresses as it is read, holding only the last 4 KiB of output (as far back as Yaz0 refers): for archives too
// big to decompress whole on the console. Same bytes as decompress().
class Stream {
public:
    explicit Stream(ByteView data);  // throws FormatError if not Yaz0
    std::size_t size() const { return size_; }
    std::size_t position() const { return pos_; }
    void read(std::uint8_t* dst, std::size_t n);  // the next n bytes; throws past the end or for a damaged stream
    void skip(std::size_t n);

private:
    std::uint8_t next();
    ByteView src_;
    std::size_t s_ = 16, size_ = 0, pos_ = 0;
    std::uint8_t code_ = 0;
    int bits_ = 0;
    std::size_t copy_left_ = 0, copy_dist_ = 0;
    std::uint8_t ring_[0x1000] = {};
};

// Greedy LZ with hash chains and one-step lazy matching.
Bytes compress(ByteView data, std::uint32_t alignment = 0);

// The same for input that is produced as it is compressed (an archive that is never put together in memory): `read`
// is called with the next n bytes to fill, in order, `size` bytes in all. Same output as compress() on those bytes.
using Reader = std::function<void(std::uint8_t* dst, std::size_t n)>;
Bytes compress(std::size_t size, const Reader& read, std::uint32_t alignment = 0);

}  // namespace wiiumenu::yaz0
