#ifndef CONCORD_CHARACTER_H
#define CONCORD_CHARACTER_H

#include "Concord/CExport.h"
#include "engine/animation/SkeletalBlend.h"
#include "engine/animation/SkeletalStateMachine.h"
#include "engine/character/CharacterConfig.h"
#include "engine/object/Node.h"
#include "math/Vector3.h"

namespace Concord::Object {

class SkinnedModel;
class Camera;
class Collider;

/**
 * A camera-driven, walk/run/jump player character — the one-liner an app spawns
 * to get a controllable rigged figure on the ground.
 *
 * Built from a CharacterConfig (`scene.Spawn<Character>({.model = "hero.glb"})`),
 * it hides the pieces a hand-rolled controller would juggle: it imports the
 * rigged model, wires an idle/walk/run blend space plus an optional jump state
 * into a SkeletalStateMachine, follows the body with a first- or third-person
 * Camera, and carries a capsule-like Collider. Every frame it samples WASD and
 * the mouse, moves along the ground, applies gravity/jump, turns the body
 * toward travel, and drives the animation from the resulting speed and
 * airborne state.
 *
 * The Character owns its own OnStart/OnUpdate: it spawns its sub-nodes the
 * first frame its scene ticks (a Node's OwningScene is not linked yet in the
 * constructor) and drives them every frame after. Callers steer it through the
 * small API below (Jump, SetMouseLookEnabled) and read state
 * (Velocity/Speed/IsGrounded); the state machine, skinning and movement stay
 * internal.
 *
 * @note There is currently no Scene::Despawn, so a Character's sub-nodes live
 *       until the whole Scene is destroyed.
 */
class CENGINE_API Character : public Node {
public:
    /** Builds a character from @p config; sub-nodes spawn on the first tick. */
    explicit Character(Gameplay::CharacterConfig config = {});

    /** Launches a jump if the character is currently grounded; otherwise a no-op. */
    void Jump();

    /** True while the feet rest on the ground plane (not mid-jump/fall). */
    bool IsGrounded() const noexcept { return m_grounded; }

    /** Current world-space velocity (horizontal travel plus vertical jump/fall). */
    Vector3 Velocity() const noexcept;

    /** Current horizontal ground speed in units/second. */
    float Speed() const noexcept { return m_speed; }

    /**
     * Enables or disables mouse-look. Turn it off while the OS cursor is
     * released (e.g. after Esc) so stray mouse motion doesn't swing the camera.
     */
    void SetMouseLookEnabled(bool enabled) noexcept { m_mouseLook = enabled; }

    /** True while mouse-look is driving the camera. */
    bool IsMouseLookEnabled() const noexcept { return m_mouseLook; }

    /**
     * Enables or disables keyboard/mouse control. While disabled the character
     * ignores WASD/Space/mouse input and eases to a standing idle (gravity,
     * animation and camera follow still update). Use this when another view is
     * active so a shared control scheme doesn't drive the character too.
     */
    void SetControlEnabled(bool enabled) noexcept { m_controlEnabled = enabled; }

    /** True while the character responds to keyboard/mouse input. */
    bool IsControlEnabled() const noexcept { return m_controlEnabled; }

    /** The skinned figure this character drives, or nullptr before the first tick. */
    SkinnedModel* Model() const noexcept { return m_model; }

    /** The camera following this character, or nullptr before the first tick. */
    Camera* View() const noexcept { return m_camera; }

    /** The configuration this character was built from. */
    const Gameplay::CharacterConfig& Config() const noexcept { return m_config; }

private:
    /** OnStart hook: spawns the model/camera/collider and wires the state machine. */
    void Initialize();

    /** OnUpdate hook: samples input, moves, animates and repositions the camera. */
    void Tick(float deltaTime);

    void SampleLook();
    void UpdateMovement(float deltaTime);
    void UpdateVertical(float deltaTime);
    void UpdateAnimation(float deltaTime);
    void UpdateCamera();

    Gameplay::CharacterConfig m_config;

    SkinnedModel* m_model = nullptr;
    Camera* m_camera = nullptr;
    Collider* m_collider = nullptr;

    Animation::SkeletalBlendSpace1D m_locomotion;
    Animation::SkeletalStateMachine m_stateMachine;
    bool m_hasStateMachine = false;
    bool m_hasJumpState = false;

    Vector3 m_feet{};            // world-space foot position
    Vector3 m_moveDir{0.0f, 0.0f, 1.0f};
    float m_bodyYaw = 0.0f;      // degrees, facing direction
    float m_cameraYaw = 0.0f;    // degrees, look/orbit yaw
    float m_cameraPitch = 12.0f; // degrees, look/orbit pitch
    float m_speed = 0.0f;        // current horizontal speed
    float m_verticalVelocity = 0.0f;
    bool m_grounded = true;
    bool m_jumpQueued = false;
    bool m_mouseLook = true;
    bool m_controlEnabled = true;
};

} // namespace Concord::Object

#endif // CONCORD_CHARACTER_H
