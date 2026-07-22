#ifndef CONCORD_WINDOWID_H
#define CONCORD_WINDOWID_H

#include "Concord/CExport.h"

#include <cstdint>

namespace Concord {

/**
 * @brief Process-unique handle for one engine-managed window.
 *
 * Values are allocated once for the process lifetime and are never reused after
 * an EngineLoop reacquire, so event subscribers can treat them as stable source
 * identities across Game / loop generations. Zero is reserved as invalid.
 */
using WindowId = std::uint64_t;

/** @brief Sentinel for "no window". */
inline constexpr WindowId kInvalidWindowId = 0;

/**
 * @brief Allocates the next process-unique window id.
 *
 * Safe to call from any thread. Never returns `kInvalidWindowId`.
 */
CENGINE_API WindowId AllocateWindowId();

} // namespace Concord

#endif // CONCORD_WINDOWID_H
