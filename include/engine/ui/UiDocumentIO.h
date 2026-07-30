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
 * File-system adapter for the versioned Asset::CookedUiDocument format. The
 * payload uses the shared little-endian serialization codec and preserves the
 * established CUI1 byte layout. The adapter performs bounded whole-file I/O;
 * decoding completes before the destination document is replaced.
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
