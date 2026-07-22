#include "engine/asset/import/gltf/GltfSkinBuilder.h"

#include "engine/asset/import/gltf/GltfBufferReader.h"

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Asset::Gltf {

namespace {

/** A node's local matrix: its explicit `matrix` (column-major) or its TRS. */
Matrix4 NodeMatrix(const JsonValue& node)
{
    if (const JsonValue* m = node.Find("matrix"); m != nullptr && m->IsArray() && m->array.size() >= 16) {
        Matrix4 out;
        for (int k = 0; k < 16; ++k) {
            out.m[k] = static_cast<float>(m->array[static_cast<std::size_t>(k)].number);
        }
        return out;
    }
    Transform t;
    if (const JsonValue* v = node.Find("translation"); v != nullptr && v->IsArray() && v->array.size() >= 3) {
        t.position = Vector3{static_cast<float>(v->array[0].number),
                             static_cast<float>(v->array[1].number),
                             static_cast<float>(v->array[2].number)};
    }
    if (const JsonValue* v = node.Find("rotation"); v != nullptr && v->IsArray() && v->array.size() >= 4) {
        t.rotation = Quaternion{static_cast<float>(v->array[0].number),
                                static_cast<float>(v->array[1].number),
                                static_cast<float>(v->array[2].number),
                                static_cast<float>(v->array[3].number)};
    }
    if (const JsonValue* v = node.Find("scale"); v != nullptr && v->IsArray() && v->array.size() >= 3) {
        t.scale = Vector3{static_cast<float>(v->array[0].number),
                          static_cast<float>(v->array[1].number),
                          static_cast<float>(v->array[2].number)};
    }
    return Matrix4::FromTransform(t);
}

/** Reads a glTF node's TRS into a Concord Transform (matrix nodes unsupported for bones). */
Transform ReadNodeTransform(const JsonValue& node)
{
    Transform t;
    if (const JsonValue* v = node.Find("translation"); v != nullptr && v->IsArray() && v->array.size() >= 3) {
        t.position = Vector3{static_cast<float>(v->array[0].number),
                             static_cast<float>(v->array[1].number),
                             static_cast<float>(v->array[2].number)};
    }
    if (const JsonValue* v = node.Find("rotation"); v != nullptr && v->IsArray() && v->array.size() >= 4) {
        t.rotation = Quaternion{static_cast<float>(v->array[0].number),
                                static_cast<float>(v->array[1].number),
                                static_cast<float>(v->array[2].number),
                                static_cast<float>(v->array[3].number)};
    }
    if (const JsonValue* v = node.Find("scale"); v != nullptr && v->IsArray() && v->array.size() >= 3) {
        t.scale = Vector3{static_cast<float>(v->array[0].number),
                          static_cast<float>(v->array[1].number),
                          static_cast<float>(v->array[2].number)};
    }
    return t;
}

} // namespace

Animation::Skeleton BuildSkeleton(const JsonValue& skin,
                                  const std::vector<JsonValue>& nodes,
                                  const std::vector<JsonValue>& accessors,
                                  const std::vector<std::vector<std::uint8_t>>& buffers,
                                  const std::vector<JsonValue>& bufferViews,
                                  std::vector<int>& nodeToBone)
{
    Animation::Skeleton skeleton;
    nodeToBone.assign(nodes.size(), -1);

    const JsonValue* joints = skin.Find("joints");
    if (joints == nullptr || !joints->IsArray() || joints->array.empty()) {
        return skeleton;
    }

    // joints[i] -> node index; bone i corresponds to joints[i].
    std::vector<int> jointNodes;
    jointNodes.reserve(joints->array.size());
    for (const JsonValue& j : joints->array) {
        if (j.IsNumber()) {
            const int nodeIdx = j.IntegerOr(-1);
            const int boneIdx = static_cast<int>(jointNodes.size());
            jointNodes.push_back(nodeIdx);
            if (nodeIdx >= 0 && nodeIdx < static_cast<int>(nodeToBone.size())) {
                nodeToBone[nodeIdx] = boneIdx;
            }
        }
    }

    // childNode -> parentNode, from every node's children list.
    std::unordered_map<int, int> parentOf;
    for (int n = 0; n < static_cast<int>(nodes.size()); ++n) {
        if (const JsonValue* children = nodes[n].Find("children");
            children != nullptr && children->IsArray()) {
            for (const JsonValue& c : children->array) {
                if (c.IsNumber()) {
                    const int child = c.IntegerOr(-1);
                    if (child >= 0 && child < static_cast<int>(nodes.size())) {
                        parentOf[child] = n;
                    }
                }
            }
        }
    }

    const int ibmAccessor = skin.IntOr("inverseBindMatrices", -1);
    const bool hasIbm = ibmAccessor >= 0 && ibmAccessor < static_cast<int>(accessors.size());

    skeleton.bones.reserve(jointNodes.size());
    for (std::size_t i = 0; i < jointNodes.size(); ++i) {
        const int nodeIdx = jointNodes[i];
        Animation::Bone bone;
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(nodes.size())) {
            bone.name = nodes[nodeIdx].StrOr("name", "bone" + std::to_string(i));
            bone.bindLocal = ReadNodeTransform(nodes[nodeIdx]);
        }
        // Parent bone: the joint whose node is this node's parent, else -1.
        bone.parent = -1;
        if (const auto it = parentOf.find(nodeIdx); it != parentOf.end()) {
            const int parentNode = it->second;
            if (parentNode >= 0 && parentNode < static_cast<int>(nodeToBone.size())) {
                bone.parent = nodeToBone[parentNode];
            }
        }
        // Inverse bind matrix (column-major MAT4, used as-is), else identity.
        if (hasIbm) {
            const std::array<float, 16> m = ReadAccessor(accessors[ibmAccessor], buffers, bufferViews, i);
            for (int k = 0; k < 16; ++k) {
                bone.inverseBind.m[k] = m[k];
            }
        }
        skeleton.bones.push_back(std::move(bone));
    }

    // Root pre-transform: the world transform of the root joint's non-joint
    // ancestors (e.g. an armature/scene node that rotates the whole character
    // upright). The joint chain alone omits it, which leaves rigs lying down.
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        if (skeleton.bones[i].parent >= 0) {
            continue;
        }
        std::vector<int> chain; // ancestors of the root joint, nearest first
        int cur = jointNodes[i];
        for (auto it = parentOf.find(cur); it != parentOf.end(); it = parentOf.find(cur)) {
            chain.push_back(it->second);
            cur = it->second;
        }
        Matrix4 m; // identity
        for (auto rit = chain.rbegin(); rit != chain.rend(); ++rit) {
            if (*rit >= 0 && *rit < static_cast<int>(nodes.size())) {
                m = Matrix4::Multiply(m, NodeMatrix(nodes[*rit]));
            }
        }
        skeleton.rootTransform = m;
        break; // first root joint carries the shared pre-transform
    }
    return skeleton;
}

void ReadSkinAttributes(const JsonValue& primitive,
                        const std::vector<JsonValue>& accessors,
                        const std::vector<std::vector<std::uint8_t>>& buffers,
                        const std::vector<JsonValue>& bufferViews,
                        MeshData& geometry)
{
    const JsonValue* attrs = primitive.Find("attributes");
    if (attrs == nullptr || !attrs->IsObject()) {
        return;
    }
    const int jointIdx = attrs->IntOr("JOINTS_0", -1);
    const int weightIdx = attrs->IntOr("WEIGHTS_0", -1);
    if (jointIdx < 0 || jointIdx >= static_cast<int>(accessors.size())
        || weightIdx < 0 || weightIdx >= static_cast<int>(accessors.size())) {
        return;
    }

    const std::size_t count = geometry.positions.size();
    geometry.boneIndices.resize(count);
    geometry.boneWeights.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::array<float, 16> j = ReadAccessor(accessors[jointIdx], buffers, bufferViews, i);
        const std::array<float, 16> w = ReadAccessor(accessors[weightIdx], buffers, bufferViews, i);
        geometry.boneIndices[i] = {static_cast<std::uint16_t>(j[0]),
                                   static_cast<std::uint16_t>(j[1]),
                                   static_cast<std::uint16_t>(j[2]),
                                   static_cast<std::uint16_t>(j[3])};
        float sum = w[0] + w[1] + w[2] + w[3];
        if (sum <= 1e-6f) {
            // Degenerate weights: bind fully to the first influence.
            geometry.boneWeights[i] = {1.0f, 0.0f, 0.0f, 0.0f};
        } else {
            const float inv = 1.0f / sum;
            geometry.boneWeights[i] = {w[0] * inv, w[1] * inv, w[2] * inv, w[3] * inv};
        }
    }
}

Animation::SkeletalClip BuildSkeletalClip(const JsonValue& animation,
                                          const std::vector<JsonValue>& accessors,
                                          const std::vector<std::vector<std::uint8_t>>& buffers,
                                          const std::vector<JsonValue>& bufferViews,
                                          const std::vector<int>& nodeToBone)
{
    Animation::SkeletalClip clip;
    clip.name = animation.StrOr("name", "clip");

    const JsonValue* channels = animation.Find("channels");
    const JsonValue* samplers = animation.Find("samplers");
    if (channels == nullptr || !channels->IsArray() || samplers == nullptr || !samplers->IsArray()) {
        return clip;
    }

    // One BoneTrack per animated bone, found/created on demand.
    std::unordered_map<int, std::size_t> boneToTrack;
    auto trackFor = [&](int bone) -> Animation::BoneTrack& {
        const auto it = boneToTrack.find(bone);
        if (it != boneToTrack.end()) {
            return clip.tracks[it->second];
        }
        Animation::BoneTrack track;
        track.boneIndex = bone;
        boneToTrack.emplace(bone, clip.tracks.size());
        clip.tracks.push_back(std::move(track));
        return clip.tracks.back();
    };

    for (const JsonValue& channel : channels->array) {
        const int samplerIdx = channel.IntOr("sampler", -1);
        const JsonValue* target = channel.Find("target");
        if (samplerIdx < 0 || samplerIdx >= static_cast<int>(samplers->array.size()) || target == nullptr) {
            continue;
        }
        const int node = target->IntOr("node", -1);
        if (node < 0 || node >= static_cast<int>(nodeToBone.size()) || nodeToBone[node] < 0) {
            continue;
        }
        const int bone = nodeToBone[node];
        const std::string path = target->StrOr("path", "");

        const JsonValue& sampler = samplers->array[samplerIdx];
        const int inputIdx = sampler.IntOr("input", -1);
        const int outputIdx = sampler.IntOr("output", -1);
        if (inputIdx < 0 || inputIdx >= static_cast<int>(accessors.size())
            || outputIdx < 0 || outputIdx >= static_cast<int>(accessors.size())) {
            continue;
        }
        const std::string interp = sampler.StrOr("interpolation", "LINEAR");
        const int stride = interp == "CUBICSPLINE" ? 3 : 1;
        const int center = interp == "CUBICSPLINE" ? 1 : 0; // central value of the triplet

        const JsonValue& inputAcc = accessors[inputIdx];
        const int keyCount = inputAcc.IntOr("count", 0);
        Animation::BoneTrack& track = trackFor(bone);

        for (int i = 0; i < keyCount; ++i) {
            const float time = ReadAccessor(inputAcc, buffers, bufferViews, static_cast<std::size_t>(i))[0];
            const std::size_t valueElem = static_cast<std::size_t>(i * stride + center);
            const std::array<float, 16> v = ReadAccessor(accessors[outputIdx], buffers, bufferViews, valueElem);
            if (path == "translation") {
                track.position.AddKey(time, Vector3{v[0], v[1], v[2]});
            } else if (path == "rotation") {
                track.rotation.AddKey(time, Quaternion{v[0], v[1], v[2], v[3]});
            } else if (path == "scale") {
                track.scale.AddKey(time, Vector3{v[0], v[1], v[2]});
            }
        }
    }
    return clip;
}

} // namespace Concord::Asset::Gltf
