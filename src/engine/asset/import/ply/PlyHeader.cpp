#include "engine/asset/import/ply/PlyHeader.h"

#include "engine/asset/import/ply/PlyLimits.h"
#include "engine/debug/Logger.h"

#include <cstring>
#include <limits>
#include <sstream>

namespace Concord::Asset::Ply {

int TypeSize(PlyType t) noexcept
{
    switch (t) {
        case PlyType::Char: case PlyType::UChar:   return 1;
        case PlyType::Short: case PlyType::UShort: return 2;
        case PlyType::Int:   case PlyType::UInt:
        case PlyType::Float:                       return 4;
        case PlyType::Double:                      return 8;
    }
    return 4;
}

bool ParseType(std::string_view text, PlyType& type) noexcept
{
    if (text == "char" || text == "int8") type = PlyType::Char;
    else if (text == "uchar" || text == "uint8") type = PlyType::UChar;
    else if (text == "short" || text == "int16") type = PlyType::Short;
    else if (text == "ushort" || text == "uint16") type = PlyType::UShort;
    else if (text == "int" || text == "int32") type = PlyType::Int;
    else if (text == "uint" || text == "uint32") type = PlyType::UInt;
    else if (text == "float" || text == "float32") type = PlyType::Float;
    else if (text == "double" || text == "float64") type = PlyType::Double;
    else return false;
    return true;
}

double ReadScalar(const std::uint8_t* p, PlyType t) noexcept
{
    switch (t) {
        case PlyType::Char: { std::int8_t v;  std::memcpy(&v, p, 1); return v; }
        case PlyType::UChar: { std::uint8_t v; std::memcpy(&v, p, 1); return v; }
        case PlyType::Short: { std::int16_t v; std::memcpy(&v, p, 2); return v; }
        case PlyType::UShort: { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
        case PlyType::Int: { std::int32_t v; std::memcpy(&v, p, 4); return v; }
        case PlyType::UInt: { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
        case PlyType::Float: { float v; std::memcpy(&v, p, 4); return v; }
        case PlyType::Double: { double v; std::memcpy(&v, p, 8); return v; }
    }
    return 0.0;
}

bool ParseHeader(std::istream& in, const std::string& path, PlyHeader& out)
{
    out = PlyHeader{};
    PlyElement* current = nullptr;
    bool foundEndHeader = false;

    std::string line;
    if (!std::getline(in, line)) {
        Debug::Logger::Warn("Asset", "PLY: missing 'ply' magic in '%s'", path.c_str());
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line != "ply") {
        Debug::Logger::Warn("Asset", "PLY: missing 'ply' magic in '%s'", path.c_str());
        return false;
    }
    while (std::getline(in, line)) {
        // Strip a trailing CR so CRLF files parse cleanly.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream ss(line);
        std::string kw;
        ss >> kw;
        if (kw == "end_header") {
            foundEndHeader = true;
            break;
        }
        if (kw == "comment" || kw == "obj_info") {
            continue;
        }
        if (kw == "format") {
            std::string fmt;
            if (!(ss >> fmt)) {
                Debug::Logger::Warn("Asset", "PLY: malformed format in '%s'", path.c_str());
                return false;
            }
            out.binary = (fmt == "binary_little_endian" || fmt == "binary_big_endian");
        } else if (kw == "element") {
            std::string name;
            std::uint64_t count = 0;
            if (!(ss >> name >> count) || count > std::numeric_limits<std::uint32_t>::max()) {
                Debug::Logger::Warn("Asset", "PLY: malformed element declaration in '%s'", path.c_str());
                return false;
            }
            if (out.elements.size() >= Limits::MaxElementCount) {
                Debug::Logger::Warn("Asset", "PLY: too many elements in '%s' (limit %zu)",
                                    path.c_str(), Limits::MaxElementCount);
                return false;
            }
            const std::uint64_t countLimit = name == "vertex"
                ? Limits::MaxVertexCount
                : Limits::MaxFaceCount;
            if (count > countLimit) {
                Debug::Logger::Warn("Asset", "PLY: element '%s' count %llu exceeds limit %llu in '%s'",
                                    name.c_str(), static_cast<unsigned long long>(count),
                                    static_cast<unsigned long long>(countLimit), path.c_str());
                return false;
            }
            out.elements.push_back(PlyElement{name, static_cast<std::uint32_t>(count), {}});
            current = &out.elements.back();
        } else if (kw == "property" && current != nullptr) {
            if (current->properties.size() >= Limits::MaxPropertyCount) {
                Debug::Logger::Warn("Asset", "PLY: too many properties on element '%s' in '%s' (limit %zu)",
                                    current->name.c_str(), path.c_str(), Limits::MaxPropertyCount);
                return false;
            }
            std::string t1;
            if (!(ss >> t1)) {
                Debug::Logger::Warn("Asset", "PLY: malformed property in '%s'", path.c_str());
                return false;
            }
            PlyProperty prop;
            if (t1 == "list") {
                std::string ct, it, nm;
                if (!(ss >> ct >> it >> nm) ||
                    !ParseType(ct, prop.countType) || !ParseType(it, prop.type)) {
                    Debug::Logger::Warn("Asset", "PLY: invalid list property in '%s'", path.c_str());
                    return false;
                }
                prop.isList = true;
                prop.name = nm;
            } else {
                std::string nm;
                if (!(ss >> nm) || !ParseType(t1, prop.type)) {
                    Debug::Logger::Warn("Asset", "PLY: invalid scalar property in '%s'", path.c_str());
                    return false;
                }
                prop.name = nm;
            }
            current->properties.push_back(std::move(prop));
        }
    }
    if (!foundEndHeader) {
        Debug::Logger::Warn("Asset", "PLY: missing end_header in '%s'", path.c_str());
    }
    return foundEndHeader;
}

} // namespace Concord::Asset::Ply
