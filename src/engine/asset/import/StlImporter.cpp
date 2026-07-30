#include "engine/asset/import/StlImporter.h"

#include "engine/debug/Logger.h"
#include "engine/material/MaterialDesc.h"
#include "engine/render/mesh/MeshData.h"
#include "math/Vector3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Asset {

namespace {

/**
 * De-duplicates Vector3 keys. glTF/OBJ importers reach for the same pattern;
 * this struct carries both the hash and the bit-exact equality so an
 * unordered_map<Vector3, ...> compiles without a Vector3::operator==.
 */
struct Vec3Hash {
    std::size_t operator()(const Vector3& v) const noexcept
    {
        std::uint64_t h = 1469598103934665603ULL;
        std::uint32_t bits[3];
        std::memcpy(bits, &v, sizeof(bits));
        for (int i = 0; i < 3; ++i) {
            h ^= bits[i];
            h *= 1099511628211ULL;
        }
        return static_cast<std::size_t>(h);
    }
};

struct Vec3Equal {
    bool operator()(const Vector3& a, const Vector3& b) const noexcept
    {
        // Bit-exact comparison: vertices that round to the same floats are the same.
        return std::memcmp(&a, &b, sizeof(Vector3)) == 0;
    }
};

/** Reads a little-endian float from a byte pointer (binary STL). */
float ReadLeFloat(const std::uint8_t* p) noexcept
{
    float v;
    std::memcpy(&v, p, sizeof(float));
    return v;
}

/**
 * Imports a binary STL: 80-byte header, uint32 triangle count, then 50 bytes
 * per facet (normal + 3 vertices + attribute). Returns true on success.
 */
bool ImportBinary(std::ifstream& file, ImportedModel& model)
{
    std::uint8_t header[80];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    std::uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (count == 0) {
        return false;
    }

    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<std::uint32_t> indices;
    std::unordered_map<Vector3, std::uint32_t, Vec3Hash, Vec3Equal> lookup;
    lookup.reserve(count * 3);
    positions.reserve(count * 3);
    normals.reserve(count * 3);
    indices.reserve(count * 3);

    std::uint8_t buf[50];
    for (std::uint32_t t = 0; t < count; ++t) {
        file.read(reinterpret_cast<char*>(buf), sizeof(buf));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(buf))) {
            Debug::Logger::Warn("Asset", "STL: truncated at triangle %u/%u", t, count);
            break;
        }
        const Vector3 normal{ReadLeFloat(buf + 0), ReadLeFloat(buf + 4), ReadLeFloat(buf + 8)};
        for (int v = 0; v < 3; ++v) {
            const std::uint8_t* vp = buf + 12 + v * 12;
            Vector3 pos{ReadLeFloat(vp), ReadLeFloat(vp + 4), ReadLeFloat(vp + 8)};
            auto it = lookup.find(pos);
            std::uint32_t idx;
            if (it != lookup.end()) {
                idx = it->second;
                // Accumulate the normal so shared corners smooth.
                Vector3& n = normals[idx];
                n.x += normal.x; n.y += normal.y; n.z += normal.z;
            } else {
                idx = static_cast<std::uint32_t>(positions.size());
                positions.push_back(pos);
                normals.push_back(normal);
                lookup.emplace(pos, idx);
            }
            indices.push_back(idx);
        }
    }

    // Normalize accumulated normals (unreferenced corners keep their face normal).
    for (Vector3& n : normals) {
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) {
            n.x /= len; n.y /= len; n.z /= len;
        } else {
            n = Vector3{0.0f, 1.0f, 0.0f};
        }
    }

    ImportedSubMesh sub;
    sub.geometry.positions = std::move(positions);
    sub.geometry.normals = std::move(normals);
    if (sub.geometry.positions.size() <= 65535) {
        sub.geometry.indices.reserve(indices.size());
        for (std::uint32_t i : indices) {
            sub.geometry.indices.push_back(static_cast<std::uint16_t>(i));
        }
    } else {
        sub.geometry.indices32 = std::move(indices);
    }
    model.meshes.push_back(std::move(sub));
    return true;
}

/**
 * Imports an ASCII STL by scanning for `vertex` triples and `facet normal`
 * triples. Robust to whitespace and keyword case variations.
 */
bool ImportAscii(std::ifstream& file, ImportedModel& model)
{
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<std::uint32_t> indices;
    std::unordered_map<Vector3, std::uint32_t, Vec3Hash, Vec3Equal> lookup;

    Vector3 currentNormal{0.0f, 1.0f, 0.0f};
    std::string token;
    bool haveNormal = false;

    while (file >> token) {
        if (token == "facet" || token == "valid") {
            // Read up to the normal values: "facet normal nx ny nz".
            std::string next;
            if (file >> next && next == "normal") {
                file >> currentNormal.x >> currentNormal.y >> currentNormal.z;
                haveNormal = true;
            }
        } else if (token == "vertex") {
            Vector3 pos{};
            file >> pos.x >> pos.y >> pos.z;
            auto it = lookup.find(pos);
            std::uint32_t idx;
            if (it != lookup.end()) {
                idx = it->second;
                Vector3& n = normals[idx];
                n.x += currentNormal.x; n.y += currentNormal.y; n.z += currentNormal.z;
            } else {
                idx = static_cast<std::uint32_t>(positions.size());
                positions.push_back(pos);
                normals.push_back(haveNormal ? currentNormal : Vector3{0.0f, 1.0f, 0.0f});
                lookup.emplace(pos, idx);
            }
            indices.push_back(idx);
        }
    }

    if (indices.size() < 3 || indices.size() % 3 != 0) {
        Debug::Logger::Warn("Asset", "STL: ASCII parse yielded %zu indices (not triangles)",
                            indices.size());
    }
    if (indices.empty()) {
        return false;
    }

    for (Vector3& n : normals) {
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) {
            n.x /= len; n.y /= len; n.z /= len;
        } else {
            n = Vector3{0.0f, 1.0f, 0.0f};
        }
    }

    ImportedSubMesh sub;
    sub.geometry.positions = std::move(positions);
    sub.geometry.normals = std::move(normals);
    if (sub.geometry.positions.size() <= 65535) {
        sub.geometry.indices.reserve(indices.size());
        for (std::uint32_t i : indices) {
            sub.geometry.indices.push_back(static_cast<std::uint16_t>(i));
        }
    } else {
        sub.geometry.indices32 = std::move(indices);
    }
    model.meshes.push_back(std::move(sub));
    return true;
}

} // namespace

bool StlImporter::SupportsExtension(std::string_view ext) const
{
    return ext == "stl";
}

ImportedModel StlImporter::Import(const std::string& path, ImportContext& context)
{
    (void)context;
    ImportedModel model;
    model.name = path;

    // Sniff ASCII vs binary: ASCII files start with "solid" (case-insensitive
    // in practice, but the spec says lowercase). Binary files may also begin
    // with "solid" in their 80-byte header, so we additionally check the file
    // size against the binary triangle count to disambiguate.
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Debug::Logger::Error("Asset", "STL: could not open '%s'", path.c_str());
        return model;
    }

    // Read the first 512 bytes to sniff; ASCII STL is human-readable here.
    std::string head(512, '\0');
    file.read(&head[0], head.size());
    const std::streamsize read = file.gcount();
    head.resize(static_cast<std::size_t>(read));
    file.clear();
    file.seekg(0, std::ios::beg);

    // If the declared triangle count * 50 + 84 exactly matches the file size,
    // it is definitively binary even if it starts with "solid".
    bool binary = true;
    if (read >= 84) {
        std::uint32_t count = 0;
        file.seekg(80, std::ios::beg);
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        file.seekg(0, std::ios::beg);
        file.clear();
        std::ifstream sizeProbe(path, std::ios::binary | std::ios::ate);
        const std::streamoff fileSize = sizeProbe.tellg();
        if (fileSize == static_cast<std::streamoff>(84 + static_cast<std::uint64_t>(count) * 50)) {
            binary = true;
        } else if (head.rfind("solid", 0) == 0 && head.find("facet") != std::string::npos) {
            binary = false;
        } else {
            binary = true; // default to binary when ambiguous
        }
    } else if (head.rfind("solid", 0) == 0) {
        binary = false;
    }

    const bool ok = binary ? ImportBinary(file, model) : ImportAscii(file, model);
    if (!ok) {
        Debug::Logger::Warn("Asset", "STL: import produced no geometry from '%s'", path.c_str());
        return model;
    }

    // Compute bounds from the single sub-mesh.
    if (!model.meshes.empty()) {
        const auto& pos = model.meshes[0].geometry.positions;
        if (!pos.empty()) {
            model.boundsMin = model.boundsMax = pos[0];
            for (const Vector3& p : pos) {
                model.boundsMin.x = std::min(model.boundsMin.x, p.x);
                model.boundsMin.y = std::min(model.boundsMin.y, p.y);
                model.boundsMin.z = std::min(model.boundsMin.z, p.z);
                model.boundsMax.x = std::max(model.boundsMax.x, p.x);
                model.boundsMax.y = std::max(model.boundsMax.y, p.y);
                model.boundsMax.z = std::max(model.boundsMax.z, p.z);
            }
        }
    }
    return model;
}

} // namespace Concord::Asset
