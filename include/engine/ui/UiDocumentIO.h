#ifndef CONCORD_UIDOCUMENTIO_H
#define CONCORD_UIDOCUMENTIO_H

#include "Concord/CExport.h"
#include "engine/ui/UiDocument.h"

#include <cstddef>
#include <string>

namespace Concord::UI {

/**
 * Binary load/save for UI documents in the .cui format.
 *
 * Little-endian POD stream guarded by a magic + version (same discipline as the
 * .cscene scene format): bumping the version invalidates old files rather than
 * silently mis-parsing them. The format is compact and read/written in a single
 * linear pass for fast load times.
 */
namespace UiDocumentIO {

/** Maximum encoded .cui file size accepted by Save() and Load(). */
inline constexpr std::size_t kMaxFileBytes = 16u * 1024u * 1024u;

/** Maximum number of widgets in one document. */
inline constexpr std::size_t kMaxWidgetCount = 8192u;

/** Maximum UTF-8 byte length of one widget's text payload. */
inline constexpr std::size_t kMaxWidgetTextBytes = 4096u;

/** Serializes @p document to @p path (creating parent directories). Returns success. */
CENGINE_API bool Save(const UiDocument& document, const std::string& path);

/**
 * Loads a .cui file into @p document (replacing its contents). Returns false and
 * leaves @p document unchanged on any validation, allocation, or I/O failure.
 */
CENGINE_API bool Load(UiDocument& document, const std::string& path);

} // namespace UiDocumentIO

} // namespace Concord::UI

#endif // CONCORD_UIDOCUMENTIO_H
