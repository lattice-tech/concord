#ifndef CONCORD_ISAVEABLE_H
#define CONCORD_ISAVEABLE_H

namespace Concord::Save {

class SaveArchive;

/**
 * @brief Protocol for objects that persist runtime state into save slots.
 *
 * `SaveState` writes into the archive's current section; `LoadState` reads
 * back from the same section. Implementations must tolerate older archives:
 * check `archive.SectionBytesRemaining()` before reading fields added in
 * later format revisions and fall back to defaults when absent.
 */
class ISaveable {
public:
    virtual ~ISaveable() = default;

    /** Returns false if the object could not capture a consistent snapshot. */
    virtual bool SaveState(SaveArchive& archive) = 0;

    /** Returns false if the archived state is unusable; the object should
     *  then keep (or reset to) a valid default state. */
    virtual bool LoadState(SaveArchive& archive) = 0;
};

} // namespace Concord::Save

#endif // CONCORD_ISAVEABLE_H
