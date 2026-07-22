#include "engine/object/Camera.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace Concord::Object {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

/** Highest pitch magnitude allowed, keeping the view just shy of straight up/down. */
constexpr float kMaxPitch = 89.0f;

Vector3 Normalize(Vector3 v) noexcept
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

Vector3 Cross(const Vector3& a, const Vector3& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

} // namespace

Camera::Camera(CameraDesc desc)
    : m_up(desc.up)
    , m_projection(desc.projection)
    , m_fovYDegrees(desc.fovYDegrees)
    , m_orthoHeight(desc.orthoHeight)
    , m_nearPlane(desc.nearPlane)
    , m_farPlane(desc.farPlane)
{
    SetPosition(desc.position);
    LookAt(desc.target);
}

void Camera::SetYaw(float degrees)
{
    m_yaw = degrees;
}

void Camera::SetPitch(float degrees)
{
    m_pitch = std::clamp(degrees, -kMaxPitch, kMaxPitch);
}

void Camera::AddYaw(float degrees)
{
    m_yaw += degrees;
}

void Camera::AddPitch(float degrees)
{
    m_pitch = std::clamp(m_pitch + degrees, -kMaxPitch, kMaxPitch);
}

void Camera::LookAt(Vector3 target)
{
    const Vector3 dir = Normalize({target.x - LocalTransform().position.x,
                                   target.y - LocalTransform().position.y,
                                   target.z - LocalTransform().position.z});
    const float clampedY = std::clamp(dir.y, -1.0f, 1.0f);
    m_pitch = std::clamp(std::asin(clampedY) * kRadToDeg, -kMaxPitch, kMaxPitch);
    m_yaw = std::atan2(dir.x, dir.z) * kRadToDeg;
}

void Camera::SetUp(Vector3 up)
{
    m_up = up;
}

void Camera::SetPerspective(float fovYDegrees, float nearPlane, float farPlane)
{
    m_projection = Projection::Perspective;
    m_fovYDegrees = fovYDegrees;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
}

void Camera::SetOrthographic(float height, float nearPlane, float farPlane)
{
    m_projection = Projection::Orthographic;
    m_orthoHeight = height;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
}

void Camera::SetFov(float fovYDegrees)
{
    m_fovYDegrees = fovYDegrees;
}

Vector3 Camera::ForwardFromAngles() const noexcept
{
    const float yaw = m_yaw * kDegToRad;
    const float pitch = m_pitch * kDegToRad;
    const float cosPitch = std::cos(pitch);
    return Normalize({cosPitch * std::sin(yaw), std::sin(pitch), cosPitch * std::cos(yaw)});
}

Vector3 Camera::Forward() const noexcept
{
    return ForwardFromAngles();
}

Vector3 Camera::Right() const noexcept
{
    return Normalize(Cross(m_up, ForwardFromAngles()));
}

Vector3 Camera::Up() const noexcept
{
    const Vector3 forward = ForwardFromAngles();
    return Normalize(Cross(forward, Cross(m_up, forward)));
}

void Camera::MoveForward(float distance)
{
    const Vector3 f = ForwardFromAngles();
    Translate({f.x * distance, f.y * distance, f.z * distance});
}

void Camera::MoveRight(float distance)
{
    const Vector3 r = Right();
    Translate({r.x * distance, r.y * distance, r.z * distance});
}

void Camera::MoveUp(float distance)
{
    Translate({m_up.x * distance, m_up.y * distance, m_up.z * distance});
}

void Camera::AdvanceEffects(float deltaTime)
{
    m_effects.Advance(deltaTime);
}

bool Camera::GetCameraView(CameraView& out) const
{
    const Vector3 eye = WorldPosition();
    const Vector3 forward = ForwardFromAngles();
    const bx::Vec3 eyeV{eye.x, eye.y, eye.z};
    const bx::Vec3 atV{eye.x + forward.x, eye.y + forward.y, eye.z + forward.z};
    const bx::Vec3 upV{m_up.x, m_up.y, m_up.z};
    bx::mtxLookAt(out.viewMatrix, eyeV, atV, upV);

    out.eye[0] = eye.x;
    out.eye[1] = eye.y;
    out.eye[2] = eye.z;

    out.projection = m_projection;
    out.fovYDegrees = m_fovYDegrees;
    out.orthoHeight = m_orthoHeight;
    out.nearPlane = m_nearPlane;
    out.farPlane = m_farPlane;
    out.effects = m_effects.Snapshot();
    return true;
}

} // namespace Concord::Object
