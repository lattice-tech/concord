#ifndef CONCORD_SAVEARCHIVE_H
#define CONCORD_SAVEARCHIVE_H

#include "Concord/CExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Save {

/** Stable section identifiers used by the engine's own save segments. */
namespace SaveSection {
inline constexpr std::uint32_t kMeta = 0x4154454Du;     // 'META'
inline constexpr std::uint32_t kWorld = 0x444C5257u;    // 'WRLD'
inline constexpr std::uint32_t kEntities = 0x53544E45u; // 'ENTS'
inline constexpr std::uint32_t kPlayer = 0x52594C50u;   // 'PLYR'
inline constexpr std::uint32_t kAudio = 0x4F494441u;    // 'ADIO'
} // namespace SaveSection

/**
 * @brief Little-endian binary save container with a versioned section table.
 *
 * Layout: magic 'CSAV', format version, section count, section table
 * (id/offset/size per entry), then the section payloads. Readers address
 * sections by id through the table, so unknown sections written by newer
 * engine versions are skipped for free and section order never matters.
 *
 * One instance is either a writer (BeginWrite -> sections -> Finish/SaveToFile)
 * or a reader (OpenFromFile/OpenFromBuffer -> EnterSection -> reads); the
 * `Ok()` flag latches on the first error so callers can check once at the end.
 */
class CENGINE_API SaveArchive {
public:
    static constexpr std::uint32_t kMagic = 0x56415343u; // 'CSAV'
    static constexpr std::uint32_t kFormatVersion = 1;

    // --- writing -----------------------------------------------------------
    void BeginWrite();
    bool BeginSection(std::uint32_t sectionId);
    void EndSection();

    void WriteU8(std::uint8_t value);
    void WriteBool(bool value);
    void WriteU32(std::uint32_t value);
    void WriteI32(std::int32_t value);
    void WriteU64(std::uint64_t value);
    void WriteF32(float value);
    void WriteF64(double value);
    void WriteString(const std::string& value);
    void WriteBytes(const void* data, std::size_t size);

    /** Serialises header + table + payload into `out`. */
    bool Finish(std::vector<std::uint8_t>& out);
    /** Finishes and writes atomically: `<path>.tmp` then rename over `path`. */
    bool SaveToFile(const std::string& path);

    // --- reading -----------------------------------------------------------
    bool OpenFromBuffer(std::vector<std::uint8_t> buffer);
    bool OpenFromFile(const std::string& path);
    std::uint32_t FormatVersion() const noexcept { return m_formatVersion; }
    bool HasSection(std::uint32_t sectionId) const noexcept;
    /** Positions the read cursor at the start of the section's payload. */
    bool EnterSection(std::uint32_t sectionId) noexcept;
    std::size_t SectionBytesRemaining() const noexcept;

    std::uint8_t ReadU8();
    bool ReadBool();
    std::uint32_t ReadU32();
    std::int32_t ReadI32();
    std::uint64_t ReadU64();
    float ReadF32();
    double ReadF64();
    std::string ReadString();
    bool ReadBytes(void* data, std::size_t size);

    // --- state -------------------------------------------------------------
    bool Ok() const noexcept { return m_ok; }
    void Clear() noexcept;

private:
    struct SectionEntry {
        std::uint32_t id = 0;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    void AppendLittleEndian(std::uint64_t value, std::size_t bytes);
    std::uint64_t ReadLittleEndian(std::size_t bytes);
    bool CheckRead(std::size_t bytes) noexcept;

    std::vector<std::uint8_t> m_payload;
    std::vector<SectionEntry> m_sections;
    std::size_t m_cursor = 0;
    std::size_t m_sectionEnd = 0;
    std::uint32_t m_formatVersion = kFormatVersion;
    bool m_writing = false;
    bool m_inSection = false;
    bool m_ok = true;
};

} // namespace Concord::Save

#endif // CONCORD_SAVEARCHIVE_H
