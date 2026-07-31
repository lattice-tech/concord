#ifndef CONCORD_COOKEDMODELSOURCE_H
#define CONCORD_COOKEDMODELSOURCE_H

#include "Concord/CExport.h"
#include "engine/asset/cook/CookManifest.h"
#include "engine/asset/import/ImportedModel.h"

#include <string>

namespace Concord::Asset {

/**
 * @brief Runtime view over one cooked package (output directory + manifest).
 *
 * The consuming half of the offline cook pipeline: opens the cooked output
 * directory and its manifest, then serves fully parsed models by virtual path
 * without ever touching the development source formats. Every load verifies
 * the blob on disk against the manifest's recorded output hash, so a
 * truncated or tampered cooked file is rejected instead of trusted.
 *
 * Install an opened source into ModelLoader (SetCookedSource) to make every
 * model import try the cooked package first and fall back to live source
 * import on a miss — development machines without a cook run keep working,
 * shipped builds skip the importers entirely.
 */
class CENGINE_API CookedModelSource {
public:
    /**
     * Opens the package rooted at `outputRoot` using the manifest at
     * `manifestPath`. Returns false and sets `errorOut` when the manifest is
     * missing or malformed; the source then stays closed.
     */
    bool Open(const std::string& outputRoot, const std::string& manifestPath,
              std::string& errorOut);

    /** Forgets the manifest; TryLoadModel misses until the next Open. */
    void Close() noexcept;

    bool IsOpen() const noexcept { return m_open; }

    /**
     * Loads the cooked model recorded for `virtualPath` (the same
     * project-relative reference used at cook time). Returns false — without
     * logging — when the source is closed, the path has no record, the blob
     * fails its hash check, or the blob is not a CookedModel container (e.g.
     * a skinned passthrough), so the caller can fall back to live import.
     */
    bool TryLoadModel(const std::string& virtualPath, ImportedModel& out) const;

private:
    std::string m_root;
    CookManifest m_manifest;
    bool m_open = false;
};

} // namespace Concord::Asset

#endif // CONCORD_COOKEDMODELSOURCE_H
