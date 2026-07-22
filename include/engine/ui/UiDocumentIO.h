#ifndef CONCORD_UIDOCUMENTIO_H
#define CONCORD_UIDOCUMENTIO_H

#include "Concord/CExport.h"
#include "engine/ui/UiDocument.h"

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

/** Serializes @p document to @p path (creating parent directories). Returns success. */
CENGINE_API bool Save(const UiDocument& document, const std::string& path);

/**
 * Loads a .cui file into @p document (replacing its contents). Returns false and
 * leaves @p document empty on any I/O, magic, version or truncation error.
 */
CENGINE_API bool Load(UiDocument& document, const std::string& path);

} // namespace UiDocumentIO

} // namespace Concord::UI

#endif // CONCORD_UIDOCUMENTIO_H
