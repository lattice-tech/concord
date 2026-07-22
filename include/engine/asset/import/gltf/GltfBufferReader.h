#ifndef CONCORD_GLTF_BUFFERREADER_H
#define CONCORD_GLTF_BUFFERREADER_H

#include "engine/asset/import/gltf/GltfJson.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Concord::Asset::Gltf {

/** Maps a glTF accessor component type to its byte size. */
int ComponentSize(int componentType) noexcept;

/** Number of floats in one element of the given accessor type string. */
int TypeCount(std::string_view type) noexcept;

/** Reads an optional JSON size field, rejecting invalid or unrepresentable values. */
bool TryReadSize(const JsonValue& object,
                 std::string_view key,
                 std::size_t fallback,
                 std::size_t& value) noexcept;

/** Adds two sizes and returns false when the result is not representable. */
bool TryAddSize(std::size_t left, std::size_t right, std::size_t& result) noexcept;

/** Multiplies two sizes and returns false when the result is not representable. */
bool TryMultiplySize(std::size_t left, std::size_t right, std::size_t& result) noexcept;

/**
 * Reads one accessor element as a flat array of up to 16 floats.
 *
 * Resolves the accessor's bufferView (honoring byteStride for interleaved
 * layouts) and decodes the requested element by component type, normalizing
 * integer attributes per the glTF spec when the accessor is marked normalized.
 */
std::array<float, 16> ReadAccessor(const JsonValue& accessor,
                                   const std::vector<std::vector<std::uint8_t>>& buffers,
                                   const std::vector<JsonValue>& bufferViews,
                                   std::size_t elementIndex);

/** True if `uri` is a base64 data URI ("data:...;base64,..."). */
bool IsDataUri(std::string_view uri);

/** Decodes the base64 payload of a data URI into raw bytes. */
std::vector<std::uint8_t> DecodeDataUri(std::string_view uri);

/** Loads a buffer: embedded GLB chunk, base64 data URI, or external file. */
std::vector<std::uint8_t> LoadBuffer(const JsonValue& buffer,
                                     const std::string& dir,
                                     const std::vector<std::uint8_t>& glbBin);

} // namespace Concord::Asset::Gltf

#endif // CONCORD_GLTF_BUFFERREADER_H
