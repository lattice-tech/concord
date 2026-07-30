#include "engine/asset/cook/CookManifest.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <string>

namespace Concord::Asset {

namespace {

constexpr std::string_view kMagic = "CCOOK";
constexpr std::uint32_t kManifestVersion = 1;

/** Appends a 32-hex content hash and a space separator. */
void AppendHash(std::string& out, const AssetContentHash& hash)
{
    out.append(hash.ToHex());
    out.push_back(' ');
}

std::optional<AssetContentHash> ParseHash(std::string_view hex) noexcept
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
            return std::nullopt;
        }
        lanes[i / 16] = (lanes[i / 16] << 4) | nibble;
    }
    return AssetContentHash{lanes[0], lanes[1]};
}

bool HashIsUsable(const AssetContentHash& hash) noexcept
{
    return hash.IsValid();
}

} // namespace

bool CookManifest::Put(const CookRecord& record)
{
    if (!record.id.IsValid() || !HashIsUsable(record.resolvedHash)
        || !HashIsUsable(record.outputHash)) {
        return false;
    }
    m_records[record.id.Key()] = record;
    return true;
}

const CookRecord* CookManifest::Find(const AssetId& id) const
{
    const auto found = m_records.find(id.Key());
    return found == m_records.end() ? nullptr : &found->second;
}

bool CookManifest::Contains(const AssetId& id) const
{
    return m_records.contains(id.Key());
}

std::vector<CookRecord> CookManifest::Records() const
{
    std::vector<CookRecord> records;
    records.reserve(m_records.size());
    for (const auto& [key, record] : m_records) {
        records.push_back(record);
    }
    std::sort(records.begin(), records.end(),
              [](const CookRecord& lhs, const CookRecord& rhs) {
                  return lhs.id.Key() < rhs.id.Key();
              });
    return records;
}

bool CookManifest::NeedsCook(const AssetId& id, AssetContentHash resolvedHash,
                             std::uint32_t cookerVersion) const
{
    const CookRecord* record = Find(id);
    return record == nullptr
        || record->resolvedHash != resolvedHash
        || record->cookerVersion != cookerVersion;
}

std::string CookManifest::Serialize() const
{
    std::string out;
    out.append(kMagic);
    out.push_back(' ');
    out.append(std::to_string(kManifestVersion));
    out.push_back('\n');
    for (const CookRecord& record : Records()) {
        AppendHash(out, record.resolvedHash);
        AppendHash(out, record.outputHash);
        out.append(std::to_string(record.cookerVersion));
        out.push_back(' ');
        // The identity key is last and unquoted; it never contains whitespace
        // because NormalizeVirtualPath rejects it, so the newline delimits it.
        out.append(record.id.Key());
        out.push_back('\n');
    }
    return out;
}

std::optional<CookManifest> CookManifest::Deserialize(std::string_view text)
{
    constexpr Limits limits{};
    if (text.size() > limits.maxBytes) {
        return std::nullopt;
    }
    for (const unsigned char c : text) {
        // Allow only printable ASCII plus TAB/LF/CR so a hostile manifest cannot
        // smuggle control characters into identity keys or hash fields.
        if (c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        if (c < 0x20u || c == 0x7fu) {
            return std::nullopt;
        }
    }

    std::size_t pos = 0;
    const auto nextLine = [&](std::string_view& line) -> bool {
        if (pos >= text.size()) {
            return false;
        }
        const std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            line = text.substr(pos);
            pos = text.size();
        } else {
            line = text.substr(pos, end - pos);
            pos = end + 1;
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        return true;
    };

    std::string_view header;
    if (!nextLine(header)) {
        return std::nullopt;
    }
    const std::size_t space = header.find(' ');
    if (space == std::string_view::npos || header.substr(0, space) != kMagic) {
        return std::nullopt;
    }
    std::uint32_t version = 0;
    const std::string_view versionText = header.substr(space + 1);
    const auto versionEnd = versionText.data() + versionText.size();
    const auto parsed = std::from_chars(versionText.data(), versionEnd, version);
    if (parsed.ec != std::errc{} || parsed.ptr != versionEnd
        || version != kManifestVersion) {
        return std::nullopt;
    }

    CookManifest manifest;
    std::size_t recordCount = 0;
    std::string_view line;
    while (nextLine(line)) {
        if (line.empty()) {
            continue;
        }
        if (++recordCount > limits.maxRecords) {
            return std::nullopt;
        }
        std::array<std::string_view, 4> fields;
        std::size_t cursor = 0;
        for (std::size_t field = 0; field < 3; ++field) {
            const std::size_t sep = line.find(' ', cursor);
            if (sep == std::string_view::npos) {
                return std::nullopt;
            }
            fields[field] = line.substr(cursor, sep - cursor);
            cursor = sep + 1;
        }
        fields[3] = line.substr(cursor);
        if (fields[3].empty() || fields[3].size() > limits.maxKeyBytes) {
            return std::nullopt;
        }

        const auto resolvedHash = ParseHash(fields[0]);
        const auto outputHash = ParseHash(fields[1]);
        std::uint32_t cookerVersion = 0;
        const auto versionFieldEnd = fields[2].data() + fields[2].size();
        const auto cookerParsed =
            std::from_chars(fields[2].data(), versionFieldEnd, cookerVersion);
        std::optional<AssetId> id = AssetId::FromKey(fields[3]);
        if (!resolvedHash || !outputHash || !id
            || cookerParsed.ec != std::errc{}
            || cookerParsed.ptr != versionFieldEnd) {
            return std::nullopt;
        }

        CookRecord record{*id, *resolvedHash, *outputHash, cookerVersion};
        if (manifest.Contains(*id) || !manifest.Put(record)) {
            return std::nullopt;
        }
    }
    return manifest;
}

} // namespace Concord::Asset
