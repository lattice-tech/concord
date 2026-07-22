#ifndef CONCORD_CAMERA_H
#define CONCORD_CAMERA_H

#include "Concord/CExport.h"
#include "engine/effects/view/ScreenEffectStack.h"
#include "engine/object/CameraDesc.h"
#include "engine/object/Node.h"
#include "engine/render/frame/CameraView.h"
#include "math/Vector3.h"

namespace Concord {
class Scene;
}

namespace Concord::Object {

/**
 * The scene's viewpoint: a node defining where the world is seen from and how
 * it is projected.
 *
 * Created through Scene::Spawn<Camera>(desc). The first camera a scene spawns
 * becomes active automatically; Scene::SetActiveCamera chooses another. While
 * active, it drives its window's view and projection every frame.
 *
 * Position comes from the inherited Node transform, so a camera can be
 * SetParent-ed under a moving node (e.g. a character) and ride along. Its
 * orientation is a first-person **yaw/pitch** pair: AddYaw/AddPitch (drive
 * these from mouse delta for mouse-look, pitch is clamped to avoid flipping),
 * or LookAt to aim at a point. Forward/Right/Up expose the view basis and
 * MoveForward/Right/Up walk the camera along it — the pieces of an FPS
 * controller. The lens is tuned with SetPerspective/SetOrthographic/SetFov.
 */
class CENGINE_API Camera : public Node {
public:
    explicit Camera(CameraDesc desc = {});

    /** Sets the yaw (degrees, around world up). */
    void SetYaw(float degrees);

    /** Sets the pitch (degrees, clamped to just under +/-90 to avoid flipping). */
    void SetPitch(float degrees);

    /** Adds to the yaw; good for mouse-look horizontal. */
    void AddYaw(float degrees);

    /** Adds to the pitch (clamped); good for mouse-look vertical. */
    void AddPitch(float degrees);

    float Yaw() const noexcept { return m_yaw; }
    float Pitch() const noexcept { return m_pitch; }

    /** Accessors for the lens/orientation state (used by scene serialization). */
    const Vector3& UpVector() const noexcept { return m_up; }
    Projection GetProjection() const noexcept { return m_projection; }
    float FovYDegrees() const noexcept { return m_fovYDegrees; }
    float OrthoHeight() const noexcept { return m_orthoHeight; }
    float NearPlane() const noexcept { return m_nearPlane; }
    float FarPlane() const noexcept { return m_farPlane; }

    /** Aims the camera at a world point by recomputing yaw/pitch. */
    void LookAt(Vector3 target);

    /** Sets the world up direction (roll reference). */
    void SetUp(Vector3 up);

    /** Switches to a perspective lens with the given vertical FOV and clip planes. */
    void SetPerspective(float fovYDegrees, float nearPlane, float farPlane);

    /** Switches to an orthographic lens covering `height` world units vertically. */
    void SetOrthographic(float height, float nearPlane, float farPlane);

    /** Changes only the vertical field of view (perspective). */
    void SetFov(float fovYDegrees);

    /** Unit forward direction the camera looks along (world space). */
    Vector3 Forward() const noexcept;

    /** Unit right direction (world space). */
    Vector3 Right() const noexcept;

    /** Unit up direction (world space). */
    Vector3 Up() const noexcept;

    /** Walks the camera forward by `distance` along its look direction. */
    void MoveForward(float distance);

    /** Strafes the camera by `distance` along its right direction. */
    void MoveRight(float distance);

    /** Raises the camera by `distance` along its up direction. */
    void MoveUp(float distance);

    /** Returns the thread-safe controller for this camera's screen effects. */
    Concord::Effects::ScreenEffectStack& Effects() noexcept { return m_effects; }

    /** Returns the read-only controller for this camera's screen effects. */
    const Concord::Effects::ScreenEffectStack& Effects() const noexcept { return m_effects; }

private:
    friend class Concord::Scene;

    bool IsCamera() const noexcept override { return true; }
    bool GetCameraView(CameraView& out) const override;

    /** Advances transient screen effects while this is the active camera. */
    void AdvanceEffects(float deltaTime);
    Vector3 ForwardFromAngles() const noexcept;

    Concord::Effects::ScreenEffectStack m_effects;

    float m_yaw = 0.0f;   // degrees, around world up
    float m_pitch = 0.0f; // degrees, around the camera's right axis (clamped)
    Vector3 m_up{0.0f, 1.0f, 0.0f};

    Projection m_projection = Projection::Perspective;
    float m_fovYDegrees = 60.0f;
    float m_orthoHeight = 10.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 100.0f;
};

} // namespace Concord::Object

#endif // CONCORD_CAMERA_H
