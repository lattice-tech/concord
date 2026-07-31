#include "engine/save/SaveArchive.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Concord::Save {

namespace {

constexpr std::size_t kHeaderBytes = 4 + 4 + 4;      // magic + version + count
constexpr std::size_t kTableEntryBytes = 4 + 8 + 8;  // id + offset + size
constexpr std::size_t kMaxStringBytes = 64u * 1024u * 1024u;

} // namespace

void SaveArchive::BeginWrite()
{
    Clear();
    m_writing = true;
}

bool SaveArchive::BeginSection(std::uint32_t sectionId)
{
    if (!m_writing || m_inSection) {
        m_ok = false;
        return false;
    }
    for (const SectionEntry& section : m_sections) {
        if (section.id == sectionId) {
            m_ok = false;
            return false;
        }
    }
    SectionEntry entry{};
    entry.id = sectionId;
    entry.offset = m_payload.size();
    m_sections.push_back(entry);
    m_inSection = true;
    return true;
}

void SaveArchive::EndSection()
{
    if (!m_writing || !m_inSection || m_sections.empty()) {
        m_ok = false;
        return;
    }
    SectionEntry& entry = m_sections.back();
    entry.size = m_payload.size() - entry.offset;
    m_inSection = false;
}

void SaveArchive::AppendLittleEndian(std::uint64_t value, std::size_t bytes)
{
    if (!m_writing || !m_inSection) {
        m_ok = false;
        return;
    }
    for (std::size_t index = 0; index < bytes; ++index) {
        m_payload.push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xFFu));
    }
}

void SaveArchive::WriteU8(std::uint8_t value) { AppendLittleEndian(value, 1); }
void SaveArchive::WriteBool(bool value) { AppendLittleEndian(value ? 1u : 0u, 1); }
void SaveArchive::WriteU32(std::uint32_t value) { AppendLittleEndian(value, 4); }
void SaveArchive::WriteI32(std::int32_t value)
{
    AppendLittleEndian(static_cast<std::uint32_t>(value), 4);
}
void SaveArchive::WriteU64(std::uint64_t value) { AppendLittleEndian(value, 8); }
void SaveArchive::WriteF32(float value)
{
    AppendLittleEndian(std::bit_cast<std::uint32_t>(value), 4);
}
void SaveArchive::WriteF64(double value)
{
    AppendLittleEndian(std::bit_cast<std::uint64_t>(value), 8);
}

void SaveArchive::WriteString(const std::string& value)
{
    WriteU32(static_cast<std::uint32_t>(value.size()));
    WriteBytes(value.data(), value.size());
}

void SaveArchive::WriteBytes(const void* data, std::size_t size)
{
    if (!m_writing || !m_inSection) {
        m_ok = false;
        return;
    }
    if (size == 0) {
        return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_payload.insert(m_payload.end(), bytes, bytes + size);
}

bool SaveArchive::Finish(std::vector<std::uint8_t>& out)
{
    if (!m_writing || m_inSection || !m_ok) {
        m_ok = false;
        return false;
    }
    const std::size_t tableBytes = m_sections.size() * kTableEntryBytes;
    const std::size_t payloadBase = kHeaderBytes + tableBytes;
    out.clear();
    out.reserve(payloadBase + m_payload.size());
    const auto append = [&out](std::uint64_t value, std::size_t bytes) {
        for (std::size_t index = 0; index < bytes; ++index) {
            out.push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xFFu));
        }
    };
    append(kMagic, 4);
    append(kFormatVersion, 4);
    append(m_sections.size(), 4);
    for (const SectionEntry& section : m_sections) {
        append(section.id, 4);
        append(section.offset + payloadBase, 8);
        append(section.size, 8);
    }
    out.insert(out.end(), m_payload.begin(), m_payload.end());
    return true;
}

bool SaveArchive::SaveToFile(const std::string& path)
{
    std::vector<std::uint8_t> bytes;
    if (!Finish(bytes)) {
        return false;
    }
    std::error_code errorCode;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), errorCode);
    }
    const std::filesystem::path temporary(path + ".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream.good()) {
            return false;
        }
    }
    std::filesystem::rename(temporary, target, errorCode);
    if (errorCode) {
        std::filesystem::remove(temporary, errorCode);
        return false;
    }
    return true;
}

bool SaveArchive::OpenFromBuffer(std::vector<std::uint8_t> buffer)
{
    Clear();
    m_payload = std::move(buffer);
    if (m_payload.size() < kHeaderBytes) {
        m_ok = false;
        return false;
    }
    m_cursor = 0;
    m_sectionEnd = m_payload.size();
    const std::uint32_t magic = static_cast<std::uint32_t>(ReadLittleEndian(4));
    m_formatVersion = static_cast<std::uint32_t>(ReadLittleEndian(4));
    const std::uint32_t sectionCount = static_cast<std::uint32_t>(ReadLittleEndian(4));
    if (magic != kMagic || m_formatVersion == 0
        || m_formatVersion > kFormatVersion || !m_ok) {
        m_ok = false;
        return false;
    }
    const std::size_t tableBytes = static_cast<std::size_t>(sectionCount) * kTableEntryBytes;
    if (kHeaderBytes + tableBytes > m_payload.size()) {
        m_ok = false;
        return false;
    }
    m_sections.reserve(sectionCount);
    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        SectionEntry entry{};
        entry.id = static_cast<std::uint32_t>(ReadLittleEndian(4));
        entry.offset = ReadLittleEndian(8);
        entry.size = ReadLittleEndian(8);
        if (entry.offset + entry.size > m_payload.size()
            || entry.offset + entry.size < entry.offset) {
            m_ok = false;
            return false;
        }
        m_sections.push_back(entry);
    }
    return m_ok;
}

bool SaveArchive::OpenFromFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        Clear();
        m_ok = false;
        return false;
    }
    const std::streamsize size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max<std::streamsize>(size, 0)));
    if (!bytes.empty()
        && !stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
        Clear();
        m_ok = false;
        return false;
    }
    return OpenFromBuffer(std::move(bytes));
}

bool SaveArchive::HasSection(std::uint32_t sectionId) const noexcept
{
    for (const SectionEntry& section : m_sections) {
        if (section.id == sectionId) {
            return true;
        }
    }
    return false;
}

bool SaveArchive::EnterSection(std::uint32_t sectionId) noexcept
{
    if (m_writing) {
        return false;
    }
    for (const SectionEntry& section : m_sections) {
        if (section.id == sectionId) {
            m_cursor = static_cast<std::size_t>(section.offset);
            m_sectionEnd = static_cast<std::size_t>(section.offset + section.size);
            return true;
        }
    }
    return false;
}

std::size_t SaveArchive::SectionBytesRemaining() const noexcept
{
    return m_sectionEnd > m_cursor ? m_sectionEnd - m_cursor : 0;
}

bool SaveArchive::CheckRead(std::size_t bytes) noexcept
{
    if (m_writing || m_cursor + bytes > m_sectionEnd) {
        m_ok = false;
        return false;
    }
    return true;
}

std::uint64_t SaveArchive::ReadLittleEndian(std::size_t bytes)
{
    if (!CheckRead(bytes)) {
        return 0;
    }
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < bytes; ++index) {
        value |= static_cast<std::uint64_t>(m_payload[m_cursor + index]) << (index * 8u);
    }
    m_cursor += bytes;
    return value;
}

std::uint8_t SaveArchive::ReadU8() { return static_cast<std::uint8_t>(ReadLittleEndian(1)); }
bool SaveArchive::ReadBool() { return ReadLittleEndian(1) != 0; }
std::uint32_t SaveArchive::ReadU32() { return static_cast<std::uint32_t>(ReadLittleEndian(4)); }
std::int32_t SaveArchive::ReadI32()
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(ReadLittleEndian(4)));
}
std::uint64_t SaveArchive::ReadU64() { return ReadLittleEndian(8); }
float SaveArchive::ReadF32()
{
    return std::bit_cast<float>(static_cast<std::uint32_t>(ReadLittleEndian(4)));
}
double SaveArchive::ReadF64() { return std::bit_cast<double>(ReadLittleEndian(8)); }

std::string SaveArchive::ReadString()
{
    const std::uint32_t size = ReadU32();
    if (!m_ok || size > kMaxStringBytes || !CheckRead(size)) {
        m_ok = false;
        return {};
    }
    std::string value(reinterpret_cast<const char*>(m_payload.data() + m_cursor), size);
    m_cursor += size;
    return value;
}

bool SaveArchive::ReadBytes(void* data, std::size_t size)
{
    if (!CheckRead(size)) {
        return false;
    }
    std::memcpy(data, m_payload.data() + m_cursor, size);
    m_cursor += size;
    return true;
}

void SaveArchive::Clear() noexcept
{
    m_payload.clear();
    m_sections.clear();
    m_cursor = 0;
    m_sectionEnd = 0;
    m_formatVersion = kFormatVersion;
    m_writing = false;
    m_inSection = false;
    m_ok = true;
}

} // namespace Concord::Save
