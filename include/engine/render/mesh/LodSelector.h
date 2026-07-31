#ifndef CONCORD_LODSELECTOR_H
#define CONCORD_LODSELECTOR_H

#include "engine/render/frame/RenderInstance.h"
#include "engine/render/mesh/MeshHandle.h"

namespace Concord {

/**
 * Picks the mesh an instance should draw with for a camera at `eye`.
 *
 * The coarsest level whose start distance the camera has passed wins; levels
 * whose mesh is not resident yet fall back toward finer ones so a model never
 * disappears while a coarse level is still uploading. Instances that carry no
 * LOD chain return their base mesh unchanged, which keeps the call safe to
 * apply to every submitted instance.
 */
MeshHandle SelectLodMesh(const RenderInstance& instance, const float eye[3]) noexcept;

} // namespace Concord

#endif // CONCORD_LODSELECTOR_H
