#ifndef CONCORD_PLY_HEADER_H
#define CONCORD_PLY_HEADER_H

#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace Concord::Asset::Ply {

/** The PLY scalar types and their byte widths, for binary reading. */
enum class PlyType { Char, UChar, Short, UShort, Int, UInt, Float, Double };

/** Byte width of a PLY scalar type. */
int TypeSize(PlyType t) noexcept;

/** Maps a supported PLY type keyword onto its scalar type. */
bool ParseType(std::string_view text, PlyType& type) noexcept;

/** Reads a little-endian scalar of type `t` from `p` as a double. */
double ReadScalar(const std::uint8_t* p, PlyType t) noexcept;

/** A single declared property on an element (its type and, for lists, count type). */
struct PlyProperty {
    std::string name;
    bool isList = false;
    PlyType type = PlyType::Float;
    PlyType countType = PlyType::UChar;
};

/** One declared element block (e.g. "vertex", "face") and its properties. */
struct PlyElement {
    std::string name;
    std::uint32_t count = 0;
    std::vector<PlyProperty> properties;
};

/** The parsed PLY header: binary/ASCII flag and the declared elements. */
struct PlyHeader {
    bool binary = false;
    std::vector<PlyElement> elements;
};

/**
 * Parses the ASCII PLY header from `in` up to and including "end_header",
 * leaving the stream positioned at the start of the data section. Returns
 * false (and logs) if the "ply" magic is missing, the header is malformed, or
 * a declared element or property count exceeds the import limits.
 */
bool ParseHeader(std::istream& in, const std::string& path, PlyHeader& out);

} // namespace Concord::Asset::Ply

#endif // CONCORD_PLY_HEADER_H
