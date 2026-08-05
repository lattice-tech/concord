#include "engine/character/camera/CharacterCameraRig.h"

#include "engine/object/Camera.h"

#include <algorithm>
#include <cmath>

namespace Concord::Gameplay {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

} // namespace

void CharacterCameraRig::ApplyMouseLook(float deltaX, float deltaY) noexcept
{
    const float sensitivity = m_config != nullptr ? m_config->mouseSensitivity : 0.12f;
    m_yaw += deltaX * sensitivity;
    m_pitch = std::clamp(m_pitch - deltaY * sensitivity, -85.0f, 85.0f);
}

void CharacterCameraRig::Update(float deltaTime, const Vector3& feet)
{
    if (m_camera == nullptr || m_config == nullptr) {
        return;
    }
    m_camera->SetYaw(m_yaw);
    m_camera->SetPitch(m_pitch);

    if (m_config->cameraStyle == CameraStyle::FirstPerson) {
        m_camera->SetPosition(Vector3{feet.x, feet.y + m_config->eyeHeight, feet.z});
        return;
    }

    // Third person: trail behind/above the look target by cameraDistance.
    const Vector3 target = Vector3{feet.x, feet.y + m_config->cameraTargetHeight, feet.z};
    const float yawRad = m_yaw * kDegToRad;
    const float pitchRad = m_pitch * kDegToRad;
    const float cosPitch = std::cos(pitchRad);
    const Vector3 lookDir{
        cosPitch * std::sin(yawRad),
        std::sin(pitchRad),
        cosPitch * std::cos(yawRad),
    };
    m_camera->SetPosition(Vector3{
        target.x - lookDir.x * m_config->cameraDistance,
        target.y - lookDir.y * m_config->cameraDistance,
        target.z - lookDir.z * m_config->cameraDistance,
    });
}

} // namespace Concord::Gameplay
