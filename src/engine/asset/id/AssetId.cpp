#include "engine/asset/id/AssetId.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Concord::Asset {

namespace {

bool HasSchemeOrDrive(std::string_view reference) noexcept
{
    const std::size_t colon = reference.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    // Any ':' before the first separator is a drive letter or URI scheme.
    const std::size_t separator = reference.find_first_of("/\\");
    return separator == std::string_view::npos || colon < separator;
}

char LowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

std::optional<std::string> NormalizeVirtualPath(std::string_view reference)
{
    if (reference.empty()
        || reference.find('\0') != std::string_view::npos
        || HasSchemeOrDrive(reference)) {
        return std::nullopt;
    }
    if (reference.front() == '/' || reference.front() == '\\') {
        return std::nullopt; // rooted reference
    }

    std::vector<std::string> segments;
    std::string segment;
    const auto flush = [&]() -> bool {
        if (segment.empty() || segment == ".") {
            segment.clear();
            return true;
        }
        if (segment == "..") {
            if (segments.empty()) {
                return false; // escapes the project root
            }
            segments.pop_back();
            segment.clear();
            return true;
        }
        segments.push_back(std::move(segment));
        segment.clear();
        return true;
    };

    for (const char raw : reference) {
        if (raw == '/' || raw == '\\') {
            if (!flush()) {
                return std::nullopt;
            }
        } else {
            segment.push_back(LowerAscii(raw));
        }
    }
    if (!flush()) {
        return std::nullopt;
    }
    if (segments.empty()) {
        return std::nullopt;
    }

    std::string normalized;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i != 0) {
            normalized.push_back('/');
        }
        normalized.append(segments[i]);
    }
    return normalized;
}

std::optional<AssetId> AssetId::FromVirtualPath(std::string_view reference,
                                                AssetType type)
{
    if (type == AssetType::Unknown) {
        return std::nullopt;
    }
    const std::optional<std::string> normalized = NormalizeVirtualPath(reference);
    if (!normalized) {
        return std::nullopt;
    }
    std::string key(AssetTypeName(type));
    key.push_back(':');
    key.append(*normalized);
    return AssetId(type, std::move(key));
}

std::optional<AssetId> AssetId::FromGuid(std::uint64_t high, std::uint64_t low,
                                         AssetType type)
{
    if (type == AssetType::Unknown || (high == 0 && low == 0)) {
        return std::nullopt;
    }
    char hex[33];
    std::snprintf(hex, sizeof(hex), "%016llx%016llx",
                  static_cast<unsigned long long>(high),
                  static_cast<unsigned long long>(low));
    std::string key(AssetTypeName(type));
    key.append(":guid:");
    key.append(hex, 32);
    return AssetId(type, std::move(key));
}

namespace {

std::optional<std::pair<std::uint64_t, std::uint64_t>> ParseGuidHex(
    std::string_view hex) noexcept
{
    if (hex.size() != 32) {
        return std::nullopt;
    }
    std::uint64_t lanes[2] = {0, 0};
    for (std::size_t i = 0; i < 32; ++i) {
        const char c = hex[i];
        std::uint64_t nibble = 0;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<std::uint64_t>(c - 'a' + 10);
        } else {
            return std::nullopt; // only lowercase hex is canonical
        }
        lanes[i / 16] = (lanes[i / 16] << 4) | nibble;
    }
    return std::pair{lanes[0], lanes[1]};
}

} // namespace

std::optional<AssetId> AssetId::FromKey(std::string_view key)
{
    const std::size_t colon = key.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const AssetType type = AssetTypeFromName(key.substr(0, colon));
    if (type == AssetType::Unknown) {
        return std::nullopt;
    }
    const std::string_view remainder = key.substr(colon + 1);

    constexpr std::string_view kGuidPrefix = "guid:";
    if (remainder.substr(0, kGuidPrefix.size()) == kGuidPrefix) {
        const auto guid = ParseGuidHex(remainder.substr(kGuidPrefix.size()));
        if (!guid) {
            return std::nullopt;
        }
        std::optional<AssetId> id = FromGuid(guid->first, guid->second, type);
        // A round-tripped key must reproduce itself exactly; reject anything else.
        if (id && id->Key() == key) {
            return id;
        }
        return std::nullopt;
    }

    std::optional<AssetId> id = FromVirtualPath(remainder, type);
    if (id && id->Key() == key) {
        return id;
    }
    return std::nullopt;
}

} // namespace Concord::Asset
