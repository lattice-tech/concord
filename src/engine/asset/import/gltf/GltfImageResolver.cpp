#include "engine/asset/import/gltf/GltfImageResolver.h"

#include "engine/asset/import/gltf/GltfBufferReader.h"
#include "engine/asset/import/ImportPaths.h"

#include <cstdio>
#include <fstream>
#include <limits>
#include <string_view>

namespace Concord::Asset::Gltf {

namespace {

/** Writes an embedded image (data URI or bufferView bytes) to a cache file. */
std::string WriteImageToCache(const std::string& uri,
                              const std::vector<std::uint8_t>& bytes,
                              const std::string& dir,
                              std::size_t index)
{
    // Derive a file extension from the data URI mime type, default to .png.
    std::string ext = "png";
    if (IsDataUri(uri)) {
        const auto semi = uri.find(';');
        if (semi != std::string::npos) {
            const std::string_view mime = std::string_view(uri).substr(5, semi - 5);
            if (mime == "image/jpeg") ext = "jpg";
            else if (mime == "image/png") ext = "png";
            else if (mime == "image/ktx2") ext = "ktx";
        }
    }
    char name[64];
    std::snprintf(name, sizeof(name), "_gltf_img_%zu.%s", index, ext.c_str());
    const std::string path = Paths::Join(dir, name);
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open() ||
        bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        return {};
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path;
}

/**
 * Resolves a glTF image to a file path the texture loader can read, writing
 * embedded images to a cache file when necessary. Returns the resolved path
 * or an empty string on failure.
 */
std::string ResolveImagePath(const JsonValue& image,
                             const std::vector<std::vector<std::uint8_t>>& buffers,
                             const std::vector<JsonValue>& bufferViews,
                             const std::string& dir,
                             std::size_t index)
{
    const std::string uri = image.StrOr("uri", "");
    if (!uri.empty()) {
        if (IsDataUri(uri)) {
            const std::vector<std::uint8_t> bytes = DecodeDataUri(uri);
            if (bytes.empty()) {
                return {};
            }
            return WriteImageToCache(uri, bytes, dir, index);
        }
        return Paths::ResolveWithin(dir, uri);
    }
    std::size_t bufferViewIndex = bufferViews.size();
    if (!TryReadSize(image, "bufferView", bufferViews.size(), bufferViewIndex) ||
        bufferViewIndex >= bufferViews.size()) {
        return {};
    }
    const JsonValue& bv = bufferViews[bufferViewIndex];
    std::size_t bufferIndex = 0;
    if (!TryReadSize(bv, "buffer", 0, bufferIndex) || bufferIndex >= buffers.size()) {
        return {};
    }
    std::size_t offset = 0;
    std::size_t length = 0;
    std::size_t end = 0;
    if (!TryReadSize(bv, "byteOffset", 0, offset) ||
        !TryReadSize(bv, "byteLength", 0, length) || length == 0 ||
        !TryAddSize(offset, length, end)) {
        return {};
    }
    const std::vector<std::uint8_t>& buf = buffers[bufferIndex];
    if (end > buf.size() ||
        length > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        return {};
    }
    const std::string mime = image.StrOr("mimeType", "image/png");
    std::string ext = "png";
    if (mime == "image/jpeg") ext = "jpg";
    else if (mime == "image/ktx2") ext = "ktx";
    char name[64];
    std::snprintf(name, sizeof(name), "_gltf_img_%zu.%s", index, ext.c_str());
    const std::string path = Paths::Join(dir, name);
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    file.write(reinterpret_cast<const char*>(buf.data() + offset),
               static_cast<std::streamsize>(length));
    return path;
}

} // namespace

std::string ResolveTexturePath(const JsonValue& texture,
                               const std::vector<JsonValue>& images,
                               const std::vector<std::vector<std::uint8_t>>& buffers,
                               const std::vector<JsonValue>& bufferViews,
                               const std::string& dir)
{
    std::size_t source = images.size();
    if (!TryReadSize(texture, "source", images.size(), source) || source >= images.size()) {
        return {};
    }
    return ResolveImagePath(images[source], buffers, bufferViews, dir, source);
}

} // namespace Concord::Asset::Gltf
