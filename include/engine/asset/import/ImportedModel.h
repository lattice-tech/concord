#ifndef CONCORD_IMPORTEDMODEL_H
#define CONCORD_IMPORTEDMODEL_H

#include "engine/animation/clip/SkeletalClip.h"
#include "engine/animation/skeleton/Skeleton.h"
#include "engine/material/MaterialDesc.h"
#include "engine/render/mesh/MeshData.h"
#include "math/Vector3.h"

#include <string>
#include <vector>

namespace Concord::Asset {

/**
 * One piece of geometry plus the material it should be drawn with.
 *
 * A single model file typically contains several of these (one per mesh or
 * material group), each an independently drawable sub-mesh. Keeping them
 * separate preserves the artist's material split so, for example, a character
 * model's body, eyes and weapon each keep their own textures and surface
 * parameters instead of being merged into one flat-shaded lump.
 */
struct ImportedSubMesh {
    /** CPU-side geometry ready for IRenderBackend::CreateMesh. */
    MeshData geometry{};

    /** The surface description this sub-mesh's faces should be drawn with. */
    Material::MaterialDesc material{};
};

/**
 * The fully parsed result of importing one model file.
 *
 * Produced by an IModelImporter and consumed by Object::Model (and anything
 * else that wants the raw geometry). It owns every vertex and index it
 * references, so it is safe to hold and re-upload at will. The axis-aligned
 * bounds span every sub-mesh and let a caller normalize placement (center and
 * scale the whole model into a known range) without a second pass.
 */
struct ImportedModel {
    /** One entry per independently drawable mesh/material group, in file order. */
    std::vector<ImportedSubMesh> meshes;

    /** Min corner of the whole model's axis-aligned bounds (positions). */
    Vector3 boundsMin{};

    /** Max corner of the whole model's axis-aligned bounds (positions). */
    Vector3 boundsMax{};

    /** A name for diagnostics, usually the file's stem or an internal label. */
    std::string name;

    /**
     * Skeleton for a skinned model (glTF skin). Empty for static models. When
     * present, the sub-meshes' geometry carries per-vertex boneIndices/
     * boneWeights indexing these bones, and the node should be drawn as a
     * SkinnedModel rather than a static Model.
     */
    Animation::Skeleton skeleton;

    /** Skeletal animations parsed from the file (glTF animations), in file order. */
    std::vector<Animation::SkeletalClip> clips;

    /** True when at least one sub-mesh carried geometry worth drawing. */
    bool HasGeometry() const noexcept { return !meshes.empty(); }

    /** True when this model has a skeleton (skinned) rather than static geometry. */
    bool IsSkinned() const noexcept { return !skeleton.Empty(); }
};

} // namespace Concord::Asset

#endif // CONCORD_IMPORTEDMODEL_H
