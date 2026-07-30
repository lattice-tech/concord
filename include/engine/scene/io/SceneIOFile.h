#ifndef CONCORD_SCENEIOFILE_H
#define CONCORD_SCENEIOFILE_H

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Detail::SceneIo {

/** Reads one size-bounded scene file into memory. */
bool ReadSceneFile(const std::string& path, std::vector<std::uint8_t>& bytes);

/** Writes through a sibling temporary file and atomically replaces the target. */
bool WriteSceneFileAtomic(const std::string& path,
                          const std::vector<std::uint8_t>& bytes);

} // namespace Concord::Detail::SceneIo

#endif // CONCORD_SCENEIOFILE_H
