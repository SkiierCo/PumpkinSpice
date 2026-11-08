// PumpkinSpice theme packages (.ps): a theme's own work only, rebuilt on the console against its own Menu files.
// Format: PumpkinSpice docs/PS-FORMAT.md. "PSPK", u32 version, u32 manifest size, JSON manifest, data area.
//
// Archives are diffed and rebuilt member by member (recursively, through Yaz0), with the same alignment and
// compression rules as tree::build, so a rebuilt file has the same contents as one built from the theme's source.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "wiiumenu/bytes.hpp"
#include "wiiumenu/json.hpp"

namespace wiiumenu::ps {

constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 12;

// ---- reading

struct Manifest {
    json::Value doc;
    std::uint64_t data_start = 0;  // file offset of the data area
};

// manifest size from the 12-byte header (throws if it is not a .ps of a known version)
std::uint32_t manifest_size(ByteView header);
// header + manifest bytes -> manifest
Manifest read_manifest(ByteView head);

// returns `size` bytes of the data area starting at `offset` (relative to the data area)
using BlobReader = std::function<Bytes(std::uint64_t offset, std::uint64_t size)>;

// The node's blob ("data": [offset, size]) as offset/size.
std::pair<std::uint64_t, std::uint64_t> blob_range(const json::Value& node);

// The region a top-level path belongs to: "EUR", "USA" or "JPN" for a path through a language folder (EuEnglish,
// UsFrench, JpJapanese, ...: the Menu's content and the EULA's 0005001B 10050000/content/00/<language>) and for the app
// and applet titles (00050010, 00050030: the low ID's 6th digit, 0 JPN, 1 USA, 2 EUR); "" for files every region
// uses (content/Common, ...).
std::string region_of(const std::string& node_path);

// True if a top-level node is for this console's region ("EUR", "USA", "JPN"): nodes without "regions" are for all.
bool for_region(const json::Value& node, const std::string& region);

// Where the stock file of a top-level node is on the console: "/vol/content/..." or
// "/vol/storage_mlc01/<sys|usr>/title/<high>/<low>/...". Empty for an unknown path.
std::string console_path(const std::string& node_path);

// Rebuilds one top-level node. `stock` is the stock file (nullptr if the console has none).
// Returns nullopt (and why) when the node is skipped: no stock file for a patch/archive, or a stock CRC that
// does not match. Throws FormatError for a damaged package.
std::optional<Bytes> rebuild(const json::Value& node, const BlobReader& blob, const Bytes* stock, std::string* why);

// ---- making

struct Entry {
    std::string path;           // "content/..." or "titles/<high>/<low>/..."
    Bytes theme;                // the theme's finished file
    std::vector<Bytes> stocks;  // every known stock version (e.g. Nintendo's and Pretendo's text); none: added whole
};

struct Report {
    std::vector<std::string> added;      // top-level files without a stock file (stored whole)
    std::vector<std::string> unchanged;  // top-level files equal to stock (left out)
    std::vector<std::string> whole;      // stock files that could only be stored as one patch (not member by member)
    std::size_t patches = 0, member_adds = 0, removes = 0;
};

// Builds the package: one node per stock version of a file (same path; a console uses the first whose stock file
// matches its own). Every node is checked by rebuilding it and comparing contents with the theme's file.
Bytes create(const std::string& name, const std::string& code, const std::vector<Entry>& entries, Report& report);

// True if two files have the same contents: equal bytes, or archives whose members have the same contents
// (compression and member order may differ).
bool same_content(ByteView a, ByteView b);

}  // namespace wiiumenu::ps
