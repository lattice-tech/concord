#include "engine/asset/import/dae/DaeNodeWalker.h"

#include "engine/debug/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace Concord::Asset::Dae {

namespace {

/** Sets a column-major 4x4 to the identity. */
void Identity(float* m) noexcept
{
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/** out = lhs * rhs (column-major 4x4 multiply). */
void Multiply(float* out, const float* lhs, const float* rhs) noexcept
{
    float tmp[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += lhs[k * 4 + row] * rhs[col * 4 + k];
            }
            tmp[col * 4 + row] = sum;
        }
    }
    std::memcpy(out, tmp, sizeof(tmp));
}

/** Parses a space-separated list of floats (the body of <matrix>/<translate>/etc.). */
std::vector<float> ParseFloats(const std::string& text)
{
    std::vector<float> out;
    std::istringstream ss(text);
    float v;
    while (ss >> v) {
        out.push_back(v);
    }
    return out;
}

/** Builds a column-major translation matrix from a <translate> element. */
void BuildTranslate(const XmlNode& node, float* out) noexcept
{
    Identity(out);
    const auto v = ParseFloats(node.text);
    if (v.size() >= 3) {
        out[12] = v[0];
        out[13] = v[1];
        out[14] = v[2];
    }
}

/** Builds a column-major scale matrix from a <scale> element. */
void BuildScale(const XmlNode& node, float* out) noexcept
{
    Identity(out);
    const auto v = ParseFloats(node.text);
    if (v.size() >= 3) {
        out[0] = v[0];
        out[5] = v[1];
        out[10] = v[2];
    }
}

/**
 * Builds a column-major rotation matrix from a <rotate> element. Collada
 * stores rotations as "axis.x axis.y axis.z angle_degrees"; the angle is in
 * degrees (converted to radians here) using the right-hand rule.
 */
void BuildRotate(const XmlNode& node, float* out) noexcept
{
    Identity(out);
    const auto v = ParseFloats(node.text);
    if (v.size() < 4) {
        return;
    }
    float x = v[0], y = v[1], z = v[2];
    const float angle = v[3] * 3.14159265358979323846f / 180.0f;
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0f) {
        x /= len; y /= len; z /= len;
    }
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float t = 1.0f - c;
    // Column-major rotation about a normalized axis.
    out[0] = t * x * x + c;
    out[1] = t * x * y + s * z;
    out[2] = t * x * z - s * y;
    out[4] = t * x * y - s * z;
    out[5] = t * y * y + c;
    out[6] = t * y * z + s * x;
    out[8] = t * x * z + s * y;
    out[9] = t * y * z - s * x;
    out[10] = t * z * z + c;
}

/**
 * Reads a <matrix> element (Collada stores 16 values in row-major order) and
 * transposes it into the column-major layout the rest of the engine uses.
 */
void BuildMatrix(const XmlNode& node, float* out) noexcept
{
    const auto v = ParseFloats(node.text);
    if (v.size() < 16) {
        Identity(out);
        return;
    }
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            out[col * 4 + row] = v[row * 4 + col];
        }
    }
}

/**
 * Composes every transform child of `node` (in document order) into one
 * column-major local matrix. Collada post-multiplies, so the first transform
 * in the file is applied first: local = T1 * T2 * T3 * ...
 */
void ComposeLocalTransform(const XmlNode& node, float* out) noexcept
{
    Identity(out);
    float step[16];
    for (const XmlNode& child : node.children) {
        if (child.name == "matrix") {
            BuildMatrix(child, step);
        } else if (child.name == "translate") {
            BuildTranslate(child, step);
        } else if (child.name == "rotate") {
            BuildRotate(child, step);
        } else if (child.name == "scale") {
            BuildScale(child, step);
        } else {
            continue;
        }
        Multiply(out, out, step);
    }
}

/** Collects the material bindings from a <bind_material> under <instance_geometry>. */
std::vector<std::pair<std::string, std::string>> CollectBindings(const XmlNode& instance)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (const XmlNode* bind = instance.FindChild("bind_material")) {
        if (const XmlNode* tc = bind->FindChild("technique_common")) {
            for (const XmlNode* im : tc->FindChildren("instance_material")) {
                out.emplace_back(im->Attr("symbol"), im->Attr("target"));
            }
        }
    }
    return out;
}

/** Finds a <node> by id across <library_nodes> and <library_visual_scenes>. */
const XmlNode* FindNodeById(const XmlNode& root, std::string_view id)
{
    for (const XmlNode* lib : root.FindChildren("library_nodes")) {
        for (const XmlNode* node : lib->FindChildren("node")) {
            if (node->Attr("id") == id) {
                return node;
            }
        }
    }
    for (const XmlNode* lib : root.FindChildren("library_visual_scenes")) {
        for (const XmlNode* scene : lib->FindChildren("visual_scene")) {
            for (const XmlNode* node : scene->FindChildren("node")) {
                if (node->Attr("id") == id) {
                    return node;
                }
            }
        }
    }
    return nullptr;
}

/**
 * Recursively walks one <node>, composing its world transform with the
 * parent's and collecting every <instance_geometry> (and <instance_node>
 * sub-tree) it encounters.
 */
void WalkNode(const XmlNode& node,
              const float parentWorld[16],
              const XmlNode& root,
              std::vector<DaeNodeInstance>& out)
{
    float local[16];
    ComposeLocalTransform(node, local);
    float world[16];
    Multiply(world, parentWorld, local);

    // <instance_geometry> directly under this node.
    for (const XmlNode* inst : node.FindChildren("instance_geometry")) {
        DaeNodeInstance entry;
        entry.geometryUrl = inst->Attr("url");
        entry.materialBindings = CollectBindings(*inst);
        std::memcpy(entry.transform.data(), world, sizeof(float) * 16);
        out.push_back(std::move(entry));
    }

    // <instance_node> references another <node> by url; follow it so instanced
    // sub-trees (common in Collada) are included with the current world transform.
    for (const XmlNode* inst : node.FindChildren("instance_node")) {
        std::string url = inst->Attr("url");
        if (!url.empty() && url[0] == '#') {
            url.erase(0, 1);
        }
        if (const XmlNode* target = FindNodeById(root, url)) {
            WalkNode(*target, world, root, out);
        }
    }

    // Child <node> elements.
    for (const XmlNode* child : node.FindChildren("node")) {
        WalkNode(*child, world, root, out);
    }
}

} // namespace

std::vector<DaeNodeInstance> CollectInstances(const XmlNode& root)
{
    std::vector<DaeNodeInstance> out;

    // Find the active visual scene: <scene><instance_visual_scene url="#S"/>.
    const XmlNode* sceneDecl = root.FindChild("scene");
    std::string sceneUrl;
    if (sceneDecl != nullptr) {
        if (const XmlNode* ivs = sceneDecl->FindChild("instance_visual_scene")) {
            sceneUrl = ivs->Attr("url");
        }
    }

    float identity[16];
    Identity(identity);

    const XmlNode* visualScene = nullptr;
    if (!sceneUrl.empty()) {
        if (sceneUrl[0] == '#') {
            sceneUrl.erase(0, 1);
        }
        for (const XmlNode* lib : root.FindChildren("library_visual_scenes")) {
            for (const XmlNode* vs : lib->FindChildren("visual_scene")) {
                if (vs->Attr("id") == sceneUrl) {
                    visualScene = vs;
                    break;
                }
            }
            if (visualScene != nullptr) {
                break;
            }
        }
    }

    if (visualScene == nullptr) {
        // Fallback: use the first visual scene in the first library.
        for (const XmlNode* lib : root.FindChildren("library_visual_scenes")) {
            visualScene = lib->FindChild("visual_scene");
            if (visualScene != nullptr) {
                break;
            }
        }
    }

    if (visualScene != nullptr) {
        for (const XmlNode* node : visualScene->FindChildren("node")) {
            WalkNode(*node, identity, root, out);
        }
    } else {
        Debug::Logger::Warn("Asset", "Collada: no <visual_scene> found");
    }

    return out;
}

} // namespace Concord::Asset::Dae
