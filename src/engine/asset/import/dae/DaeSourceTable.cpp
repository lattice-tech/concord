#include "engine/asset/import/dae/DaeSourceTable.h"

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>

namespace Concord::Asset::Dae {

namespace {

/** Parses a space/newline-delimited list of floats (the body of <float_array>). */
std::vector<float> ParseFloats(std::string_view text)
{
    std::vector<float> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        char* end = nullptr;
        const float v = std::strtof(text.data() + i, &end);
        if (end == text.data() + i) {
            ++i;
            continue;
        }
        out.push_back(v);
        i = static_cast<std::size_t>(end - text.data());
    }
    return out;
}

/** Parses a space/newline-delimited list of integers (<int_array>/<Name_array> are ints here). */
std::vector<int> ParseInts(std::string_view text)
{
    std::vector<int> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        char* end = nullptr;
        const long v = std::strtol(text.data() + i, &end, 10);
        if (end == text.data() + i) {
            ++i;
            continue;
        }
        out.push_back(static_cast<int>(v));
        i = static_cast<std::size_t>(end - text.data());
    }
    return out;
}

/** Strips a leading '#' from a Collada URL reference, returning the bare id. */
std::string_view StripHash(std::string_view ref)
{
    if (!ref.empty() && ref[0] == '#') {
        ref.remove_prefix(1);
    }
    return ref;
}

} // namespace

void DaeSourceTable::Load(const XmlNode& mesh)
{
    for (const XmlNode* source : mesh.FindChildren("source")) {
        DaeSource src;
        src.id = source->Attr("id");

        if (const XmlNode* arr = source->FindChild("float_array")) {
            src.floats = ParseFloats(arr->text);
        } else if (const XmlNode* arr = source->FindChild("int_array")) {
            src.ints = ParseInts(arr->text);
        } else if (const XmlNode* arr = source->FindChild("Name_array")) {
            // Name arrays are not geometry data; skip parsing their values.
            (void)arr;
        }

        // The accessor declares how the flat array is grouped into elements.
        if (const XmlNode* technique = source->FindChild("technique_common")) {
            if (const XmlNode* accessor = technique->FindChild("accessor")) {
                src.stride = std::stoi(accessor->Attr("stride", "1"));
            }
        }
        if (src.stride < 1) {
            src.stride = 1;
        }
        m_sources.push_back(std::move(src));
    }
}

const DaeSource* DaeSourceTable::Find(std::string_view ref) const noexcept
{
    const std::string_view id = StripHash(ref);
    for (const DaeSource& s : m_sources) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace Concord::Asset::Dae
