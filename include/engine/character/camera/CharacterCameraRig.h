#ifndef CONCORD_CHARACTERCAMERARIG_H
#define CONCORD_CHARACTERCAMERARIG_H

#include "Concord/CExport.h"
#include "engine/character/CharacterConfig.h"
#include "math/Vector3.h"

namespace Concord::Object {
class Camera;
}

namespace Concord::Gameplay {

/**
 * @brief The camera framing of one character: look state, mouse look and
 * first/third-person placement.
 *
 * Owns the look yaw/pitch, applies mouse deltas (with pitch clamping) and
 * positions the attached camera each frame — first person at the eyes,
 * third person trailing behind the look target. The placement math is the
 * same a hand-rolled controller would write; the rig keeps it in one place.
 */
class CENGINE_API CharacterCameraRig {
public:
    void SetCamera(Object::Camera* camera) noexcept { m_camera = camera; }

    /** The camera this rig drives, or nullptr. */
    Object::Camera* Camera() const noexcept { return m_camera; }

    void SetConfig(const CharacterConfig& config) noexcept { m_config = &config; }

    /** Feeds mouse motion (pixels) into the look yaw/pitch. */
    void ApplyMouseLook(float deltaX, float deltaY) noexcept;

    /** The look yaw in degrees (input reference frame and camera yaw). */
    float Yaw() const noexcept { return m_yaw; }

    /** The look pitch in degrees, clamped to [-85, 85]. */
    float Pitch() const noexcept { return m_pitch; }

    /**
     * @brief Places the camera for this frame.
     *
     * @param feet The character's feet position (world).
     * @param deltaTime Used by the third-person follow smoothing (ignored in
     *        first person); a fixed step keeps behaviour deterministic.
     */
    void Update(float deltaTime, const Vector3& feet);

private:
    Object::Camera* m_camera = nullptr;
    const CharacterConfig* m_config = nullptr;
    float m_yaw = 0.0f;
    float m_pitch = 12.0f;
};

} // namespace Concord::Gameplay

#endif // CONCORD_CHARACTERCAMERARIG_H
