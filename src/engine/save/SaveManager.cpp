#include "engine/save/SaveManager.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace Concord::Save {

namespace {

constexpr std::uint32_t kMetaVersion = 1;
constexpr const char* kArchiveFileName = "save.csav";
constexpr const char* kSlotPrefix = "slot_";

std::uint64_t NowUnixSeconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool ParseSlotIndex(const std::string& directoryName, std::int32_t& outSlot)
{
    const std::string prefix(kSlotPrefix);
    if (directoryName.size() <= prefix.size()
        || directoryName.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    std::int64_t value = 0;
    for (std::size_t index = prefix.size(); index < directoryName.size(); ++index) {
        const char character = directoryName[index];
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10 + (character - '0');
        if (value > 999999) {
            return false;
        }
    }
    outSlot = static_cast<std::int32_t>(value);
    return true;
}

} // namespace

SaveManager::SaveManager(std::string rootDirectory)
    : m_rootDirectory(std::move(rootDirectory))
{
}

std::string SaveManager::SlotDirectory(std::int32_t slot) const
{
    return m_rootDirectory + "/" + kSlotPrefix + std::to_string(slot);
}

std::string SaveManager::SlotArchivePath(std::int32_t slot) const
{
    return SlotDirectory(slot) + "/" + kArchiveFileName;
}

bool SaveManager::SlotExists(std::int32_t slot) const
{
    std::error_code errorCode;
    return slot >= 0
        && std::filesystem::is_regular_file(SlotArchivePath(slot), errorCode);
}

std::vector<SaveSlotInfo> SaveManager::ListSlots() const
{
    std::vector<SaveSlotInfo> slots;
    std::error_code errorCode;
    std::filesystem::directory_iterator iterator(m_rootDirectory, errorCode);
    if (errorCode) {
        return slots;
    }
    for (const std::filesystem::directory_entry& entry : iterator) {
        if (!entry.is_directory(errorCode)) {
            continue;
        }
        std::int32_t slot = 0;
        if (!ParseSlotIndex(entry.path().filename().string(), slot)
            || !SlotExists(slot)) {
            continue;
        }
        SaveSlotInfo info{};
        info.slot = slot;
        ReadSlotMeta(slot, info.meta);
        slots.push_back(std::move(info));
    }
    std::sort(slots.begin(), slots.end(),
              [](const SaveSlotInfo& lhs, const SaveSlotInfo& rhs) {
                  return lhs.slot < rhs.slot;
              });
    return slots;
}

bool SaveManager::ReadSlotMeta(std::int32_t slot, SaveSlotMeta& outMeta) const
{
    SaveArchive archive;
    if (!archive.OpenFromFile(SlotArchivePath(slot))) {
        return false;
    }
    return ReadMeta(archive, outMeta);
}

bool SaveManager::DeleteSlot(std::int32_t slot)
{
    if (slot < 0) {
        return false;
    }
    std::error_code errorCode;
    const std::uintmax_t removed =
        std::filesystem::remove_all(SlotDirectory(slot), errorCode);
    return !errorCode && removed > 0;
}

bool SaveManager::WriteSlot(std::int32_t slot, SaveArchive& archive, SaveSlotMeta meta)
{
    if (slot < 0) {
        return false;
    }
    if (meta.timestampUnixSeconds == 0) {
        meta.timestampUnixSeconds = NowUnixSeconds();
    }
    WriteMeta(archive, meta);
    if (!archive.Ok()) {
        return false;
    }
    return archive.SaveToFile(SlotArchivePath(slot));
}

bool SaveManager::ReadSlot(std::int32_t slot, SaveArchive& outArchive,
                           SaveSlotMeta* outMeta) const
{
    if (!outArchive.OpenFromFile(SlotArchivePath(slot))) {
        return false;
    }
    if (outMeta != nullptr && !ReadMeta(outArchive, *outMeta)) {
        return false;
    }
    return true;
}

void SaveManager::WriteMeta(SaveArchive& archive, const SaveSlotMeta& meta)
{
    if (!archive.BeginSection(SaveSection::kMeta)) {
        return;
    }
    archive.WriteU32(kMetaVersion);
    archive.WriteString(meta.sceneName);
    archive.WriteString(meta.label);
    archive.WriteU64(meta.timestampUnixSeconds);
    archive.WriteF32(meta.playSeconds);
    archive.EndSection();
}

bool SaveManager::ReadMeta(SaveArchive& archive, SaveSlotMeta& outMeta)
{
    if (!archive.EnterSection(SaveSection::kMeta)) {
        return false;
    }
    const std::uint32_t version = archive.ReadU32();
    if (!archive.Ok() || version == 0 || version > kMetaVersion) {
        return false;
    }
    outMeta.sceneName = archive.ReadString();
    outMeta.label = archive.ReadString();
    outMeta.timestampUnixSeconds = archive.ReadU64();
    outMeta.playSeconds = archive.ReadF32();
    return archive.Ok();
}

} // namespace Concord::Save
