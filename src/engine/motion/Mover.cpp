#include "engine/motion/Mover.h"

#include "engine/object/Node.h"
#include "math/EulerAngles.h"

#include <cmath>
#include <utility>

namespace Concord {
namespace Motion {

namespace {

float Length(const Vector3& v) noexcept
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

bool IsZero(const Vector3& v) noexcept
{
    return Length(v) <= 1e-6f;
}

Vector3 Lerp(const Vector3& a, const Vector3& b, float t) noexcept
{
    return a + (b - a) * t;
}

/** Shortest-arc quaternion slerp; falls back to nlerp for near-parallel inputs. */
Quaternion Slerp(Quaternion a, Quaternion b, float t) noexcept
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) { // take the shorter path
        b = Quaternion{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    Quaternion result;
    if (dot > 0.9995f) {
        // Nearly parallel: linear interpolate to avoid a divide by ~0.
        result = Quaternion{a.x + (b.x - a.x) * t,
                            a.y + (b.y - a.y) * t,
                            a.z + (b.z - a.z) * t,
                            a.w + (b.w - a.w) * t};
    } else {
        const float theta0 = std::acos(dot);
        const float theta = theta0 * t;
        const float sinTheta0 = std::sin(theta0);
        const float s0 = std::cos(theta) - dot * std::sin(theta) / sinTheta0;
        const float s1 = std::sin(theta) / sinTheta0;
        result = Quaternion{a.x * s0 + b.x * s1,
                            a.y * s0 + b.y * s1,
                            a.z * s0 + b.z * s1,
                            a.w * s0 + b.w * s1};
    }
    const float len = std::sqrt(result.x * result.x + result.y * result.y
                                + result.z * result.z + result.w * result.w);
    if (len > 1e-8f) {
        result.x /= len;
        result.y /= len;
        result.z /= len;
        result.w /= len;
    }
    return result;
}

} // namespace

Mover::Mover(Object::Node& node) noexcept : m_node(&node) {}

Mover& Mover::SetLinearVelocity(Vector3 unitsPerSecond) noexcept
{
    m_linearVelocity = unitsPerSecond;
    return *this;
}

Mover& Mover::SetAngularVelocity(Vector3 eulerDegreesPerSecond) noexcept
{
    m_angularVelocity = eulerDegreesPerSecond;
    return *this;
}

Mover& Mover::MoveTo(Vector3 target, float seconds, Easing curve)
{
    m_pathActive = false;
    m_positionTween = VectorTween{true, m_node->LocalTransform().position, target,
                                  0.0f, seconds, curve};
    return *this;
}

Mover& Mover::MoveBy(Vector3 delta, float seconds, Easing curve)
{
    m_pathActive = false;
    const Vector3 from = m_node->LocalTransform().position;
    m_positionTween = VectorTween{true, from, from + delta, 0.0f, seconds, curve};
    return *this;
}

Mover& Mover::RotateTo(Quaternion target, float seconds, Easing curve)
{
    m_rotationTween = QuatTween{true, m_node->LocalTransform().rotation, target,
                                0.0f, seconds, curve};
    return *this;
}

Mover& Mover::ScaleTo(Vector3 target, float seconds, Easing curve)
{
    m_scaleTween = VectorTween{true, m_node->LocalTransform().scale, target,
                               0.0f, seconds, curve};
    return *this;
}

Mover& Mover::FollowPath(const std::vector<Vector3>& waypoints, float unitsPerSecond, bool loop)
{
    m_positionTween.active = false;
    m_path = waypoints;
    m_pathTarget = 0;
    m_pathSpeed = unitsPerSecond;
    m_pathLoop = loop;
    m_pathActive = !m_path.empty() && unitsPerSecond > 0.0f;
    return *this;
}

void Mover::OnArrive(std::function<void()> callback)
{
    m_onArrive = std::move(callback);
}

void Mover::Stop() noexcept
{
    m_linearVelocity = Vector3{};
    m_angularVelocity = Vector3{};
    m_positionTween.active = false;
    m_rotationTween.active = false;
    m_scaleTween.active = false;
    m_pathActive = false;
}

bool Mover::IsBusy() const noexcept
{
    return m_positionTween.active || m_rotationTween.active || m_scaleTween.active || m_pathActive;
}

Vector3 Mover::StepVector(VectorTween& tween, float deltaTime, bool& completed)
{
    completed = false;
    tween.elapsed += deltaTime;
    const float raw = tween.duration > 0.0f ? tween.elapsed / tween.duration : 1.0f;
    if (raw >= 1.0f) {
        tween.active = false;
        completed = true;
        return tween.to;
    }
    return Lerp(tween.from, tween.to, Ease(tween.curve, raw));
}

void Mover::Update(float deltaTime)
{
    if (m_node == nullptr || deltaTime <= 0.0f) {
        return;
    }

    // OnArrive fires at most once per Update, even if several tweens finish in
    // the same frame, so callers see a single "step done" signal.
    bool arrived = false;

    // Position: path > tween > steady velocity (only one drives position).
    if (m_pathActive) {
        float budget = m_pathSpeed * deltaTime;
        Vector3 pos = m_node->LocalTransform().position;
        while (budget > 0.0f && m_pathActive) {
            if (m_pathTarget >= m_path.size()) {
                m_pathActive = false;
                arrived = true;
                break;
            }
            const Vector3 toTarget = m_path[m_pathTarget] - pos;
            const float dist = Length(toTarget);
            if (dist <= budget) {
                pos = m_path[m_pathTarget];
                budget -= dist;
                ++m_pathTarget;
                if (m_pathTarget >= m_path.size() && m_pathLoop) {
                    m_pathTarget = 0;
                    continue; // keep consuming the remaining budget from the first waypoint
                }
            } else {
                pos = pos + toTarget * (budget / dist);
                budget = 0.0f;
            }
        }
        m_node->SetPosition(pos);
    } else if (m_positionTween.active) {
        bool done = false;
        m_node->SetPosition(StepVector(m_positionTween, deltaTime, done));
        if (done) { arrived = true; }
    } else if (!IsZero(m_linearVelocity)) {
        m_node->Translate(m_linearVelocity * deltaTime);
    }

    // Rotation: tween > steady angular velocity.
    if (m_rotationTween.active) {
        m_rotationTween.elapsed += deltaTime;
        const float raw = m_rotationTween.duration > 0.0f
            ? m_rotationTween.elapsed / m_rotationTween.duration : 1.0f;
        if (raw >= 1.0f) {
            m_rotationTween.active = false;
            m_node->SetRotation(m_rotationTween.to);
            arrived = true;
        } else {
            m_node->SetRotation(Slerp(m_rotationTween.from, m_rotationTween.to,
                                      Ease(m_rotationTween.curve, raw)));
        }
    } else if (!IsZero(m_angularVelocity)) {
        m_node->Rotate(Quaternion::FromEuler(EulerAngles{
            m_angularVelocity.x * deltaTime,
            m_angularVelocity.y * deltaTime,
            m_angularVelocity.z * deltaTime}));
    }

    // Scale.
    if (m_scaleTween.active) {
        bool done = false;
        m_node->SetScale(StepVector(m_scaleTween, deltaTime, done));
        if (done) { arrived = true; }
    }

    if (arrived && m_onArrive) {
        m_onArrive();
    }
}

} // namespace Motion
} // namespace Concord
