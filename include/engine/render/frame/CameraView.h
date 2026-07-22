#ifndef CONCORD_CAMERAVIEW_H
#define CONCORD_CAMERAVIEW_H

#include "engine/render/frame/Projection.h"
#include "engine/render/frame/ViewEffectState.h"

namespace Concord {

/**
 * The render-thread-ready form of a camera: a world-to-view matrix plus the
 * projection parameters the backend needs.
 *
 * Produced by Object::Camera and handed to the render backend per frame. The
 * view matrix is fully baked (the camera resolves its position/target into it),
 * but the projection matrix is left to the backend so it can fold in each
 * window's live aspect ratio — which the camera cannot know and which changes
 * on resize.
 */
struct CameraView {
    /** Column-major 4x4 world-to-view matrix. */
    float viewMatrix[16]{};

    /** Eye position in world space (for view-dependent shading like speculars). */
    float eye[3]{0.0f, 0.0f, 0.0f};

    /** Which projection the backend should build. */
    Projection projection = Projection::Perspective;

    /** Vertical field of view in degrees (perspective only). */
    float fovYDegrees = 60.0f;

    /** Vertical world extent covered by the viewport (orthographic only). */
    float orthoHeight = 10.0f;

    /** Near clip plane distance. */
    float nearPlane = 0.1f;

    /** Far clip plane distance. */
    float farPlane = 100.0f;

    /** Screen-space effects evaluated by the active camera for this frame. */
    ViewEffectState effects{};
};

} // namespace Concord

#endif // CONCORD_CAMERAVIEW_H
