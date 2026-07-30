#ifndef CONCORD_COOKEDUIDOCUMENT_H
#define CONCORD_COOKEDUIDOCUMENT_H

#include "engine/ui/UiDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/** Resource ceilings enforced for an untrusted cooked UI document. */
struct CookedUiDocumentLimits {
    std::size_t maxFileBytes = 16u * 1024u * 1024u;
    std::uint32_t maxWidgets = 8192u;
    std::uint32_t maxWidgetTextBytes = 4096u;
};

/**
 * @brief Versioned little-endian binary format for a runtime UI document.
 *
 * The format stores the flat, replay-ready widget list without editor or
 * source metadata. It uses the shared Concord::Serialization codec. Version 2
 * appends each widget's image texture path; version 1 files (panels, labels and
 * buttons only) still decode, with an empty path. Encoding is deterministic for
 * content hashing and incremental cooking.
 */
namespace CookedUiDocument {

/** Validates widget semantics and encoded resource use against `limits`. */
bool Validate(const UI::UiDocument& document,
              const CookedUiDocumentLimits& limits = {});

/**
 * Encodes `document` to the deterministic cooked byte form.
 * @throws std::invalid_argument when the document violates the default or
 *         supplied resource contract.
 */
std::vector<std::uint8_t> Encode(
    const UI::UiDocument& document,
    const CookedUiDocumentLimits& limits = {});

/**
 * Decodes a cooked UI blob. Returns nullopt on bad magic/version, truncation,
 * trailing bytes, invalid widget data, duplicate button IDs, or a resource
 * ceiling violation.
 */
std::optional<UI::UiDocument> Decode(
    const std::uint8_t* data, std::size_t size,
    const CookedUiDocumentLimits& limits = {});

} // namespace CookedUiDocument

} // namespace Concord::Asset

#endif // CONCORD_COOKEDUIDOCUMENT_H
