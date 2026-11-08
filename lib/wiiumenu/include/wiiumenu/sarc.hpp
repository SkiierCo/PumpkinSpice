// SARC archive reader/writer that reproduces Nintendo's layout.
//
// Unchanged archives rebuild byte-identically: each file keeps the absolute
// alignment it had in the original. New or edited files get a safe alignment
// from their type (default_alignment). Over-aligning is always safe for the
// loader; under-aligning is not.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wiiumenu/bytes.hpp"

namespace wiiumenu::sarc {

constexpr std::uint32_t kDefaultHashMult = 0x65;

struct File {
    std::string name;
    Bytes data;
    std::uint32_t alignment = 0;  // 0 = derive from type on write
};

struct Archive {
    std::vector<File> files;
    std::uint32_t hash_mult = kDefaultHashMult;
    bool big_endian = true;
    std::uint16_t version = 0x0100;
    std::uint32_t tail_padding = 0;  // zero bytes after the last file (effect archives use this)

    static Archive parse(ByteView raw);
    Bytes write() const;
    const File* get(std::string_view name) const;
};

// The same, with each file's data a view instead of a copy (the bytes must outlive it): parsing an archive this way
// costs no copies of its files, for the console, where a Menu archive and a copy of it don't both fit in memory.
struct FileView {
    std::string name;
    ByteView data;
    std::uint32_t alignment = 0;  // 0 = derive from type on write
    // where the file is in the parsed archive; `indexed`: only that is known (parse_index), `data` is empty
    std::size_t offset = 0, size = 0;
    bool indexed = false;
};

struct ArchiveView {
    std::vector<FileView> files;
    std::uint32_t hash_mult = kDefaultHashMult;
    bool big_endian = true;
    std::uint16_t version = 0x0100;
    std::uint32_t tail_padding = 0;

    static ArchiveView parse(ByteView raw);  // the files point into raw
    // From the archive's first bytes only (through its name table, i.e. up to its data offset) and its size: the files
    // are `indexed` (offset and size, no data), for an archive that is read as a stream.
    static ArchiveView parse_index(ByteView head, std::size_t archive_size);

private:
    static ArchiveView parse_index(ByteView raw, std::size_t archive_size, bool with_data);
};

// An archive as Archive::write writes it, without putting it together: the header (padded up to the first file),
// then for each file its zero padding and its data (a view, or for an `indexed` file its place in the source
// archive), then the tail padding.
struct Layout {
    Bytes head;
    struct Piece {
        std::size_t zeros;
        ByteView data;
        bool indexed = false;
        std::size_t offset = 0, size = 0;  // indexed: where the data is in the source archive
    };
    std::vector<Piece> pieces;
    std::size_t tail = 0;
    std::size_t size = 0;  // the whole archive

    // the archive's bytes in order: `data` bytes, or (data null) `size` zero bytes or, `indexed`, the source archive's
    // bytes at `offset`
    struct Segment {
        const std::uint8_t* data;
        std::size_t size;
        bool indexed;
        std::size_t offset;
    };
    std::vector<Segment> segments() const;
    Bytes join() const;  // throws for indexed files
};

Layout layout(const ArchiveView& archive);

std::uint32_t hash(std::string_view name, std::uint32_t mult = kDefaultHashMult);
std::uint32_t default_alignment(std::string_view name, ByteView data);

}  // namespace wiiumenu::sarc
