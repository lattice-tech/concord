#include "engine/character/Character.h"

#include "engine/input/Input.h"
#include "engine/object/Camera.h"
#include "engine/object/Collider.h"
#include "engine/object/SkinnedModel.h"
#include "engine/scene/Scene.h"
#include "math/Quaternion.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Concord::Object {

Character::Character(Gameplay::CharacterConfig config)
    : m_config(std::move(config))
{
    // Sub-nodes need OwningScene(), which isn't linked until Scene::AddNode
    // runs, so defer their creation to the first tick via OnStart.
    OnStart([this] { Initialize(); });
    OnUpdate([this](float deltaTime) { Tick(deltaTime); });
    SetPosition(m_config.position);
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

    // Motor: tune from the character config; ground probing runs a downward
    // scene raycast so the character follows terrain, with the config's
    // groundY as the fallback plane.
    Gameplay::CharacterMotorConfig motorConfig;
    motorConfig.walkSpeed = m_config.walkSpeed;
    motorConfig.runSpeed = m_config.runSpeed;
    motorConfig.turnSpeedDegrees = m_config.turnSpeedDegrees;
    motorConfig.jumpHeight = m_config.jumpHeight;
    motorConfig.gravity = m_config.gravity;
    motorConfig.groundY = m_config.groundY;
    m_motor.SetConfig(motorConfig);
    m_motor.SetGroundProbe(
        [scene, this](Vector3 origin, Vector3 down, float maxDistance, float& outY) {
            Collision::RaycastFilter filter;
            filter.maxDistance = maxDistance;
            if (m_collider != nullptr) {
                // Never ground on the character's own collider.
                filter.ignoreColliderId = m_collider->Id();
            }
            Collision::RaycastHit hit;
            if (scene->RaycastClosest(Collision::Ray{origin, down}, filter, hit)) {
                outY = hit.position.y;
                return true;
            }
            return false;
        });

    // Camera rig frames the look; the config's style/distance/height apply.
    m_cameraRig.SetCamera(m_camera);
    m_cameraRig.SetConfig(m_config);

    if (m_model->IsValid()) {
        // Idle/walk/run along the speed axis; a missing clip samples bind pose.
        m_locomotion.AddClip(0.0f, m_model->FindClip(m_config.idleClip));
        m_locomotion.AddClip(m_config.walkSpeed, m_model->FindClip(m_config.walkClip));
        m_locomotion.AddClip(m_config.runSpeed, m_model->FindClip(m_config.runClip));

        m_animator.SetTarget(m_model);
        m_animator.BaseMachine().AddBlendState("locomotion", &m_locomotion, "speed");
        m_animator.BaseMachine().SetEntry("locomotion");

        if (const Animation::SkeletalClip* jump = m_model->FindClip(m_config.jumpClip)) {
            using Op = Animation::TransitionCondition::Op;
            m_animator.BaseMachine().AddState("jump", jump, Animation::PlaybackMode::Loop);
            m_animator.BaseMachine().AddTransition(
                "locomotion", "jump", {{"airborne", Op::IsTrue}}, 0.1f);
            m_animator.BaseMachine().AddTransition(
                "jump", "locomotion", {{"airborne", Op::IsFalse}}, 0.15f);
            m_hasJumpState = true;
        }
        m_hasStateMachine = true;
    }

    // Actions: animation events from the base machine feed the queue; when an
    // action ends, the machine returns to the action's state (locomotion by
    // default).
    if (m_hasStateMachine) {
        m_animator.BaseMachine().SetEventCallback([this](const Animation::SkeletalEvent& event) {
            m_actionQueue.NotifyAnimationEvent(event.name);
        });
    }
    m_actionQueue.SetEndCallback([this](const Gameplay::ActionDesc& action) {
        if (!m_hasStateMachine) {
            return;
        }
        const std::string& target = !action.returnState.empty()
            ? action.returnState
            : std::string("locomotion");
        m_animator.BaseMachine().SetState(target, 0.15f);
    });

    m_cameraRig.Update(0.0f, m_config.position);
}

void Character::Tick(float deltaTime)
{
    if (deltaTime <= 0.0f) {
        return;
    }
    if (m_mouseLook && m_controlEnabled) {
        m_cameraRig.ApplyMouseLook(Input::MouseDeltaX(), Input::MouseDeltaY());
    }

    const float inputForward = m_controlEnabled
        ? (Input::IsKeyDown(Key::W) ? 1.0f : 0.0f) - (Input::IsKeyDown(Key::S) ? 1.0f : 0.0f)
        : 0.0f;
    const float inputRight = m_controlEnabled
        ? (Input::IsKeyDown(Key::D) ? 1.0f : 0.0f) - (Input::IsKeyDown(Key::A) ? 1.0f : 0.0f)
        : 0.0f;
    const bool running = m_controlEnabled
        && (Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift));
    m_motor.SetInput(inputForward, inputRight, running);
    m_motor.SetReferenceYaw(m_cameraRig.Yaw());
    m_motor.Update(deltaTime, *this);

    m_cameraRig.Update(deltaTime, WorldPosition());

    m_actionQueue.Update(deltaTime);

    if (m_hasStateMachine) {
        m_animator.Parameters().SetFloat("speed", m_motor.Speed());
        if (m_hasJumpState) {
            m_animator.Parameters().SetBool("airborne", !m_motor.IsGrounded());
        }
        m_animator.Update(deltaTime);
    }
}

void Character::Jump()
{
    m_motor.Jump();
}

bool Character::TryAction(const Gameplay::ActionDesc& action)
{
    if (!m_actionQueue.TryStart(action)) {
        return false;
    }
    if (!action.animationState.empty() && m_hasStateMachine) {
        m_animator.BaseMachine().SetState(action.animationState, 0.1f);
    }
    return true;
}

bool Character::IsGrounded() const noexcept
{
    return m_motor.IsGrounded();
}

Vector3 Character::Velocity() const noexcept
{
    return m_motor.Velocity();
}

float Character::Speed() const noexcept
{
    return m_motor.Speed();
}

} // namespace Concord::Object
