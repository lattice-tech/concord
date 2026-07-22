#include "engine/character/Character.h"

#include "engine/input/Input.h"
#include "engine/object/Camera.h"
#include "engine/object/Collider.h"
#include "engine/object/SkinnedModel.h"
#include "engine/scene/Scene.h"
#include "math/Quaternion.h"

#include <algorithm>
#include <cmath>

namespace Concord::Object {

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
    float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return delta;
}

float HorizontalLength(const Vector3& v) noexcept
{
    return std::sqrt(v.x * v.x + v.z * v.z);
}

} // namespace

Character::Character(Gameplay::CharacterConfig config)
    : m_config(std::move(config))
{
    m_feet = m_config.position;
    // Sub-nodes need OwningScene(), which isn't linked until Scene::AddNode
    // runs, so defer their creation to the first tick via OnStart.
    OnStart([this] { Initialize(); });
    OnUpdate([this](float deltaTime) { Tick(deltaTime); });
    SetPosition(m_feet);
}

void Character::Initialize()
{
    Scene* scene = OwningScene();
    if (scene == nullptr) {
        return;
    }

    const float s = m_config.scale;
    m_model = &scene->Spawn<SkinnedModel>(SkinnedModelDesc{
        .transform = {.scale = {s, s, s}},
        .path = m_config.model,
    });
    m_model->SetParent(this);
    m_model->SetRotation(Quaternion::FromEuler({.yaw = m_config.modelYawOffsetDegrees}));

    // A capsule-like upright box (feet at 0, head near 2m) for interaction and
    // future ground queries; ShapeType has no capsule primitive yet.
    m_collider = &scene->Spawn<Collider>(ColliderDesc{
        .shape = {.type = Collision::ShapeType::Box,
                  .halfExtents = {0.35f, 1.0f, 0.35f},
                  .offset = {0.0f, 1.0f, 0.0f}},
    });
    m_collider->SetParent(this);

    m_camera = &scene->Spawn<Camera>(CameraDesc{});
    scene->SetActiveCamera(*m_camera);

    if (m_model->IsValid()) {
        // Idle/walk/run along the speed axis; a missing clip samples bind pose.
        m_locomotion.AddClip(0.0f, m_model->FindClip(m_config.idleClip));
        m_locomotion.AddClip(m_config.walkSpeed, m_model->FindClip(m_config.walkClip));
        m_locomotion.AddClip(m_config.runSpeed, m_model->FindClip(m_config.runClip));

        m_stateMachine.SetTarget(m_model);
        m_stateMachine.AddBlendState("locomotion", &m_locomotion, "speed");
        m_stateMachine.SetEntry("locomotion");

        if (const Animation::SkeletalClip* jump = m_model->FindClip(m_config.jumpClip)) {
            using Op = Animation::TransitionCondition::Op;
            m_stateMachine.AddState("jump", jump, Animation::PlaybackMode::Loop);
            m_stateMachine.AddTransition("locomotion", "jump", {{"airborne", Op::IsTrue}}, 0.1f);
            m_stateMachine.AddTransition("jump", "locomotion", {{"airborne", Op::IsFalse}}, 0.15f);
            m_hasJumpState = true;
        }
        m_hasStateMachine = true;
    }

    UpdateCamera();
}

void Character::Tick(float deltaTime)
{
    if (deltaTime <= 0.0f) {
        return;
    }
    SampleLook();
    UpdateMovement(deltaTime);
    UpdateVertical(deltaTime);

    SetPosition(m_feet);
    SetRotation(Quaternion::FromEuler({.yaw = m_bodyYaw}));

    UpdateAnimation(deltaTime);
    UpdateCamera();
}

void Character::SampleLook()
{
    if (!m_mouseLook || !m_controlEnabled) {
        return;
    }
    m_cameraYaw += Input::MouseDeltaX() * m_config.mouseSensitivity;
    m_cameraPitch -= Input::MouseDeltaY() * m_config.mouseSensitivity;
    m_cameraPitch = std::clamp(m_cameraPitch, -85.0f, 85.0f);
}

void Character::UpdateMovement(float deltaTime)
{
    const float inputForward = m_controlEnabled
        ? (Input::IsKeyDown(Key::W) ? 1.0f : 0.0f) - (Input::IsKeyDown(Key::S) ? 1.0f : 0.0f)
        : 0.0f;
    const float inputRight = m_controlEnabled
        ? (Input::IsKeyDown(Key::D) ? 1.0f : 0.0f) - (Input::IsKeyDown(Key::A) ? 1.0f : 0.0f)
        : 0.0f;

    const Vector3 forward = PlanarForward(m_cameraYaw);
    const Vector3 right = PlanarRight(m_cameraYaw);
    Vector3 desired = forward * inputForward + right * inputRight;

    const float desiredLen = HorizontalLength(desired);
    const bool moving = desiredLen > 1e-3f;
    if (moving) {
        m_moveDir = Vector3{desired.x / desiredLen, 0.0f, desired.z / desiredLen};
    }

    const bool running = m_controlEnabled
        && (Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift));
    const float targetSpeed = moving ? (running ? m_config.runSpeed : m_config.walkSpeed) : 0.0f;

    // Ease the speed so starts/stops aren't instantaneous (footfall-friendly).
    float k = deltaTime * 10.0f;
    k = std::clamp(k, 0.0f, 1.0f);
    m_speed += (targetSpeed - m_speed) * k;
    if (m_speed < 0.01f) {
        m_speed = 0.0f;
    }

    m_feet = m_feet + m_moveDir * (m_speed * deltaTime);

    // Turn the body toward travel at the configured rate.
    if (moving) {
        const float targetYaw = std::atan2(m_moveDir.x, m_moveDir.z) * kRadToDeg;
        const float delta = ShortestAngle(m_bodyYaw, targetYaw);
        const float maxStep = m_config.turnSpeedDegrees * deltaTime;
        m_bodyYaw += std::clamp(delta, -maxStep, maxStep);
    }
}

void Character::UpdateVertical(float deltaTime)
{
    const bool jumpPressed = m_jumpQueued
        || (m_controlEnabled && Input::WasKeyPressed(Key::Space));
    m_jumpQueued = false;
    if (m_grounded && jumpPressed && m_config.jumpHeight > 0.0f) {
        m_verticalVelocity = std::sqrt(2.0f * m_config.gravity * m_config.jumpHeight);
        m_grounded = false;
    }

    if (!m_grounded) {
        m_verticalVelocity -= m_config.gravity * deltaTime;
        m_feet.y += m_verticalVelocity * deltaTime;
        if (m_feet.y <= m_config.groundY) {
            m_feet.y = m_config.groundY;
            m_verticalVelocity = 0.0f;
            m_grounded = true;
        }
    } else {
        m_feet.y = m_config.groundY;
    }
}

void Character::UpdateAnimation(float deltaTime)
{
    if (!m_hasStateMachine) {
        return;
    }
    m_stateMachine.Parameters().SetFloat("speed", m_speed);
    if (m_hasJumpState) {
        m_stateMachine.Parameters().SetBool("airborne", !m_grounded);
    }
    m_stateMachine.Update(deltaTime);
}

void Character::UpdateCamera()
{
    if (m_camera == nullptr) {
        return;
    }
    m_camera->SetYaw(m_cameraYaw);
    m_camera->SetPitch(m_cameraPitch);

    if (m_config.cameraStyle == Gameplay::CameraStyle::FirstPerson) {
        m_camera->SetPosition(m_feet + Vector3{0.0f, m_config.eyeHeight, 0.0f});
        return;
    }

    // Third person: trail behind/above the look target by cameraDistance.
    const Vector3 target = m_feet + Vector3{0.0f, m_config.cameraTargetHeight, 0.0f};
    const float yawRad = m_cameraYaw * kDegToRad;
    const float pitchRad = m_cameraPitch * kDegToRad;
    const float cosPitch = std::cos(pitchRad);
    const Vector3 lookDir{cosPitch * std::sin(yawRad), std::sin(pitchRad), cosPitch * std::cos(yawRad)};
    m_camera->SetPosition(target - lookDir * m_config.cameraDistance);
}

void Character::Jump()
{
    m_jumpQueued = true;
}

Vector3 Character::Velocity() const noexcept
{
    return m_moveDir * m_speed + Vector3{0.0f, m_verticalVelocity, 0.0f};
}

} // namespace Concord::Object
