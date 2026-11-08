// BPS patches (the format flips uses): apply and create.
//
// A BPS patch records the CRC32 of the source, the target and the patch itself,
// so applying checks that the right source file was given and that the result
// is exactly the intended target.
#pragma once

#include <cstdint>
#include <string>

#include "wiiumenu/bytes.hpp"

namespace wiiumenu::bps {

// CRC32 (as in zip); pass the previous result as `previous` to continue it over more data.
std::uint32_t crc32(ByteView data, std::uint32_t previous = 0);

// Throws FormatError if the patch is damaged, the source does not match the
// patch's source CRC, or the result does not match its target CRC.
Bytes apply(ByteView patch, ByteView source);

// Builds a patch turning `source` into `target`. The result is verified by
// applying it before it is returned.
Bytes create(ByteView source, ByteView target, const std::string& metadata = "");

}  // namespace wiiumenu::bps
