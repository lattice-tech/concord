#ifndef CONCORD_CHARACTER_H
#define CONCORD_CHARACTER_H

#include "Concord/CExport.h"
#include "engine/animation/blend/SkeletalBlend.h"
#include "engine/animation/layer/SkeletalAnimator.h"
#include "engine/character/CharacterConfig.h"
#include "engine/character/camera/CharacterCameraRig.h"
#include "engine/character/motor/CharacterMotor.h"
#include "engine/gameplay/action/Action.h"
#include "engine/gameplay/action/ActionQueue.h"
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
 * into a SkeletalAnimator, follows the body with a first- or third-person
 * camera rig, and carries a capsule-like Collider. Movement lives in a
 * CharacterMotor (grounded by a scene raycast instead of a fixed plane) and
 * the camera framing in a CharacterCameraRig; this class composes them.
 *
 * The Character owns its own OnStart/OnUpdate: it spawns its sub-nodes the
 * first frame its scene ticks (a Node's OwningScene is not linked yet in the
 * constructor) and drives them every frame after. Callers steer it through the
 * small API below (Jump, SetMouseLookEnabled) and read state
 * (Velocity/Speed/IsGrounded); the animator, motor and camera stay internal.
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

    /** True while the feet rest on a surface (not mid-jump/fall). */
    bool IsGrounded() const noexcept;

    /** Current world-space velocity (horizontal travel plus vertical jump/fall). */
    Vector3 Velocity() const noexcept;

    /** Current horizontal ground speed in units/second. */
    float Speed() const noexcept;

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

    /**
     * @brief Attempts to start an action (attack, dodge, ...).
     *
     * The action runs on this character's action queue (start rules, duration,
     * cooldown, interruption; see Gameplay::ActionQueue). When the action
     * names an `animationState`, the animator's base machine jumps to it;
     * when it ends, the machine returns to the action's `returnState` (or
     * "locomotion"). Animation events matching the action's `triggerEvent`
     * reach the queue's event callback.
     * @return false when the queue rejects the action (cooldown, cannot
     *         interrupt) — nothing starts and no state is jumped.
     */
    bool TryAction(const Gameplay::ActionDesc& action);

    /** The action queue driving this character's actions. */
    Gameplay::ActionQueue& Actions() noexcept { return m_actionQueue; }

private:
    /** OnStart hook: spawns the model/camera/collider and wires the animator. */
    void Initialize();

    /** OnUpdate hook: samples input, moves, animates and repositions the camera. */
    void Tick(float deltaTime);

    Gameplay::CharacterConfig m_config;

    SkinnedModel* m_model = nullptr;
    Camera* m_camera = nullptr;
    Collider* m_collider = nullptr;

    Gameplay::CharacterMotor m_motor;
    Gameplay::CharacterCameraRig m_cameraRig;
    Gameplay::ActionQueue m_actionQueue;
    Animation::SkeletalAnimator m_animator;
    Animation::SkeletalBlendSpace1D m_locomotion;
    bool m_hasStateMachine = false;
    bool m_hasJumpState = false;

    bool m_mouseLook = true;
    bool m_controlEnabled = true;
};

} // namespace Concord::Object

#endif // CONCORD_CHARACTER_H
