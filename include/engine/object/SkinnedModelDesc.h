#ifndef CONCORD_SKINNEDMODELDESC_H
#define CONCORD_SKINNEDMODELDESC_H

#include "engine/animation/Skeleton.h"
#include "engine/material/MaterialDesc.h"
#include "engine/object/Transform.h"
#include "engine/render/mesh/MeshData.h"

namespace Concord::Object {

/**
 * Everything a SkinnedModel node is built from: a skinned mesh (geometry with
 * per-vertex bone indices/weights), the skeleton those indices reference, and
 * a material.
 *
 * A plain aggregate moved into the node. Typically produced by a glTF import
 * (skin + mesh) once that path lands, but usable directly for procedural
 * skinned meshes (the demo builds a bending column this way).
 */
struct SkinnedModelDesc {
    /** Where the node starts in the scene (parent-relative). */
    Transform transform{};

    /**
     * Path to a rigged model file (glTF/GLB with a skin). When set, the
     * skeleton, skinned sub-meshes and animation clips are imported from it and
     * the first clip auto-plays; `mesh`/`skeleton`/`material` below are ignored.
     * Leave empty to build a procedural skinned mesh from the fields below.
     *
     * Imported vertices are used as authored (no auto-normalize — that would
     * desync the bind pose); scale via `transform.scale` instead.
     */
    std::string path;

    /** Procedural skinned geometry: positions/normals/uvs + boneIndices/boneWeights. */
    MeshData mesh{};

    /** The skeleton the mesh's bone indices reference; drives the skinning palette. */
    Animation::Skeleton skeleton{};

    /** Surface the procedural mesh is drawn with. */
    Material::MaterialDesc material{};

    /** When true, `materialOverride` replaces every sub-mesh material (imported or procedural). */
    bool overrideMaterial = false;

    /** Material applied to all sub-meshes when `overrideMaterial` is set. */
    Material::MaterialDesc materialOverride{};
};

} // namespace Concord::Object

#endif // CONCORD_SKINNEDMODELDESC_H
