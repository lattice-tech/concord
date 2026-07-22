#include "engine/asset/import/obj/ObjMaterialLibrary.h"

#include "engine/asset/import/ImportPaths.h"
#include "engine/debug/Logger.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace Concord::Asset::Obj {

namespace {

/** Converts three 0..1 linear floats to a packed 0xRRGGBBAA color (opaque). */
std::uint32_t ToPackedColor(float r, float g, float b, float a = 1.0f)
{
    const auto clamp = [](float v) {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };
    const auto to8 = [&clamp](float v) {
        return static_cast<std::uint32_t>(clamp(v) * 255.0f + 0.5f);
    };
    return (to8(r) << 24) | (to8(g) << 16) | (to8(b) << 8) | to8(a);
}

} // namespace

MaterialTable ParseMtl(const std::string& mtlPath)
{
    MaterialTable out;
    std::ifstream file(mtlPath);
    if (!file.is_open()) {
        Debug::Logger::Warn("Asset", "OBJ: could not open MTL '%s'", mtlPath.c_str());
        return out;
    }
    const std::string dir = Paths::Directory(mtlPath);
    std::string line;
    ParsedMaterial* current = nullptr;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;
        if (!(ss >> token)) {
            continue;
        }
        if (token == "newmtl") {
            std::string name;
            ss >> name;
            ParsedMaterial mat;
            mat.name = name;
            current = &(out[name] = std::move(mat));
        } else if (current == nullptr) {
            continue;
        } else if (token == "Kd") {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            ss >> r >> g >> b;
            current->desc.surface.albedo = ToPackedColor(r, g, b);
        } else if (token == "Ke") {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            ss >> r >> g >> b;
            if (r != 0.0f || g != 0.0f || b != 0.0f) {
                current->desc.surface.emissive = ToPackedColor(r, g, b);
                current->desc.surface.emissiveStrength = 1.0f;
            }
        } else if (token == "d") {
            float d = 1.0f;
            ss >> d;
            // Bake dissolve into the albedo alpha; the engine has no blend yet.
            const std::uint32_t a = static_cast<std::uint32_t>(std::clamp(d, 0.0f, 1.0f) * 255.0f + 0.5f);
            current->desc.surface.albedo = (current->desc.surface.albedo & 0xFFFFFF00u) | (a & 0xFFu);
        } else if (token == "Pr") {
            float v = 0.5f;
            ss >> v;
            current->desc.surface.roughness = std::clamp(v, 0.0f, 1.0f);
        } else if (token == "Pm") {
            float v = 0.0f;
            ss >> v;
            current->desc.surface.metallic = std::clamp(v, 0.0f, 1.0f);
        } else if (token == "map_Kd") {
            std::string rest;
            std::getline(ss, rest);
            // Skip option flags (e.g. "-s 1 1 1") before the path.
            std::istringstream rs(rest);
            std::string opt;
            std::string path;
            while (rs >> opt) {
                if (opt[0] == '-') {
                    continue; // flag; its arguments are read as further tokens and skipped
                }
                path = opt;
                break;
            }
            if (!path.empty()) {
                current->desc.textures.albedo.path = Paths::Join(dir, path);
            }
        } else if (token == "map_Bump" || token == "bump") {
            std::string rest;
            std::getline(ss, rest);
            std::istringstream rs(rest);
            std::string opt;
            std::string path;
            while (rs >> opt) {
                if (opt[0] == '-') {
                    continue;
                }
                path = opt;
                break;
            }
            if (!path.empty()) {
                current->desc.textures.normal.path = Paths::Join(dir, path);
            }
        } else if (token == "map_Ke") {
            std::string rest;
            std::getline(ss, rest);
            std::istringstream rs(rest);
            std::string opt;
            std::string path;
            while (rs >> opt) {
                if (opt[0] == '-') {
                    continue;
                }
                path = opt;
                break;
            }
            if (!path.empty()) {
                current->desc.textures.emissive.path = Paths::Join(dir, path);
            }
        }
    }
    return out;
}

} // namespace Concord::Asset::Obj
