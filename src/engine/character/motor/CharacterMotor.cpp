#include "engine/character/motor/CharacterMotor.h"

#include "engine/object/Node.h"
#include "math/Quaternion.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Concord::Gameplay {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

/** Unit horizontal forward direction for a yaw in degrees (matches Camera). */
Vector3 PlanarForward(float yawDeg) noexcept
{
    const float rad = yawDeg * kDegToRad;
    return Vector3{std::sin(rad), 0.0f, std::cos(rad)};
}

/** Unit horizontal right direction for a yaw in degrees (matches Camera). */
Vector3 PlanarRight(float yawDeg) noexcept
{
    const float rad = yawDeg * kDegToRad;
    return Vector3{std::cos(rad), 0.0f, -std::sin(rad)};
}

/** Shortest signed delta (degrees) turning `from` toward `to`, in (-180, 180]. */
float ShortestAngle(float from, float to) noexcept
{
    return std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
}

float HorizontalLength(const Vector3& v) noexcept
{
    return std::sqrt(v.x * v.x + v.z * v.z);
}

} // namespace

void CharacterMotor::SetInput(float forwardAxis, float rightAxis, bool running) noexcept
{
    m_inputForward = forwardAxis;
    m_inputRight = rightAxis;
    m_running = running;
}

Vector3 CharacterMotor::Velocity() const noexcept
{
    return Vector3{m_moveDir.x * m_speed, m_verticalVelocity, m_moveDir.z * m_speed};
}

void CharacterMotor::Update(float deltaTime, Object::Node& body)
{
    if (deltaTime <= 0.0f) {
        return;
    }
    m_feet = body.WorldPosition();

    const Vector3 forward = PlanarForward(m_referenceYaw);
    const Vector3 right = PlanarRight(m_referenceYaw);
    Vector3 desired = forward * m_inputForward + right * m_inputRight;

    const float desiredLen = HorizontalLength(desired);
    const bool moving = desiredLen > 1e-3f;
    if (moving) {
        m_moveDir = Vector3{desired.x / desiredLen, 0.0f, desired.z / desiredLen};
    }

    const float targetSpeed = moving
        ? (m_running ? m_config.runSpeed : m_config.walkSpeed)
        : 0.0f;
    float k = deltaTime * m_config.acceleration;
    k = std::clamp(k, 0.0f, 1.0f);
    m_speed += (targetSpeed - m_speed) * k;
    if (m_speed < 0.01f) {
        m_speed = 0.0f;
    }

    m_feet = Vector3{
        m_feet.x + m_moveDir.x * (m_speed * deltaTime),
        m_feet.y,
        m_feet.z + m_moveDir.z * (m_speed * deltaTime),
    };

    // Turn the body toward travel at the configured rate.
    if (moving) {
        const float targetYaw = std::atan2(m_moveDir.x, m_moveDir.z) * kRadToDeg;
        const float delta = ShortestAngle(m_bodyYaw, targetYaw);
        const float maxStep = m_config.turnSpeedDegrees * deltaTime;
        m_bodyYaw += std::clamp(delta, -maxStep, maxStep);
    }

    float feetY = m_feet.y;
    UpdateVertical(deltaTime, feetY);

    body.SetPosition(Vector3{m_feet.x, feetY, m_feet.z});
    body.SetRotation(Quaternion::FromEuler({.yaw = m_bodyYaw}));
}

void CharacterMotor::UpdateVertical(float deltaTime, float& outFeetY)
{
    const bool jumpPressed = m_jumpQueued;
    m_jumpQueued = false;
    if (m_grounded && jumpPressed && m_config.jumpHeight > 0.0f) {
        m_verticalVelocity = std::sqrt(2.0f * m_config.gravity * m_config.jumpHeight);
        m_grounded = false;
    }

    if (!m_grounded) {
        m_verticalVelocity -= m_config.gravity * deltaTime;
        outFeetY += m_verticalVelocity * deltaTime;
        float hitY = 0.0f;
        if (m_groundProbe
            && m_groundProbe(Vector3{m_feet.x, outFeetY + m_config.groundProbeUpOffset,
                                     m_feet.z},
                             Vector3{0.0f, -1.0f, 0.0f},
                             m_config.groundProbeLength, hitY)
            && outFeetY <= hitY) {
            outFeetY = hitY;
            m_verticalVelocity = 0.0f;
            m_grounded = true;
        } else if (outFeetY <= m_config.groundY) {
            outFeetY = m_config.groundY;
            m_verticalVelocity = 0.0f;
            m_grounded = true;
        }
    } else {
        // Grounded: track the surface every frame so walking off a ledge
        // starts falling. Without a probe the fixed plane holds.
        float hitY = 0.0f;
        if (m_groundProbe
            && m_groundProbe(Vector3{m_feet.x, outFeetY + m_config.groundProbeUpOffset,
                                     m_feet.z},
                             Vector3{0.0f, -1.0f, 0.0f},
                             m_config.groundProbeLength, hitY)) {
            outFeetY = hitY;
        } else {
            outFeetY = m_config.groundY;
        }
    }
}

} // namespace Concord::Gameplay
