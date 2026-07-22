#ifndef CONCORD_MESHHANDLE_H
#define CONCORD_MESHHANDLE_H

#include "engine/resource/ResourceHandle.h"

namespace Concord {

/** Phantom tag distinguishing mesh handles from every other resource kind. */
struct MeshTag;

/**
 * Opaque handle to a GPU mesh owned by the render backend.
 *
 * Returned by IRenderBackend::CreateMesh and consumed by SubmitMesh /
 * DestroyMesh. It carries no backend detail (vertex/index buffers stay
 * private to the backend), so the same handle type works no matter which
 * graphics API backs it.
 */
using MeshHandle = ResourceHandle<MeshTag>;

} // namespace Concord

#endif // CONCORD_MESHHANDLE_H
