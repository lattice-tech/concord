#ifndef CONCORD_SAVEMANAGER_H
#define CONCORD_SAVEMANAGER_H

#include "Concord/CExport.h"
#include "engine/save/SaveArchive.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Save {

/** User-facing metadata stored in every slot's META section. */
struct SaveSlotMeta {
    std::string sceneName;
    std::string label;
    std::uint64_t timestampUnixSeconds = 0;
    float playSeconds = 0.0f;
};

/** Listing entry for one populated save slot. */
struct SaveSlotInfo {
    std::int32_t slot = 0;
    SaveSlotMeta meta{};
};

/**
 * @brief Owns the save directory layout: one `slot_N/` folder per slot,
 * each containing a single `save.csav` archive.
 *
 * The manager only moves whole archives; callers build the archive contents
 * (world/entity/player/audio sections) and stamp a `SaveSlotMeta`, which the
 * manager appends as the META section so listing slots never has to parse
 * gameplay data. Writes go through SaveArchive's tmp+rename path, so a crash
 * mid-save can never corrupt an existing slot.
 */
class CENGINE_API SaveManager {
public:
    explicit SaveManager(std::string rootDirectory = "bin/Data/Saves");

    const std::string& RootDirectory() const noexcept { return m_rootDirectory; }
    std::string SlotDirectory(std::int32_t slot) const;
    std::string SlotArchivePath(std::int32_t slot) const;

    bool SlotExists(std::int32_t slot) const;
    std::vector<SaveSlotInfo> ListSlots() const;
    bool ReadSlotMeta(std::int32_t slot, SaveSlotMeta& outMeta) const;
    bool DeleteSlot(std::int32_t slot);

    /**
     * Appends META built from `meta` (stamping the current time when
     * `timestampUnixSeconds` is 0) and writes the archive atomically.
     * `archive` must be a finished writer with no open section.
     */
    bool WriteSlot(std::int32_t slot, SaveArchive& archive, SaveSlotMeta meta);

    /** Opens the slot's archive for reading and extracts its META. */
    bool ReadSlot(std::int32_t slot, SaveArchive& outArchive,
                  SaveSlotMeta* outMeta = nullptr) const;

    static void WriteMeta(SaveArchive& archive, const SaveSlotMeta& meta);
    static bool ReadMeta(SaveArchive& archive, SaveSlotMeta& outMeta);

private:
    std::string m_rootDirectory;
};

} // namespace Concord::Save

#endif // CONCORD_SAVEMANAGER_H
