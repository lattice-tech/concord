#ifndef CONCORD_CHARACTERMOTOR_H
#define CONCORD_CHARACTERMOTOR_H

#include "Concord/CExport.h"
#include "math/Vector3.h"

#include <cstdint>
#include <functional>

namespace Concord::Object {
class Node;
}

namespace Concord::Gameplay {

/** Tuning knobs for one character's movement. */
struct CharacterMotorConfig {
    float walkSpeed = 2.4f;          ///< Ground speed when walking (units/s).
    float runSpeed = 6.0f;           ///< Ground speed when running (units/s).
    float turnSpeedDegrees = 720.0f; ///< How fast the body turns to face travel.
    float jumpHeight = 1.6f;         ///< Peak jump height (units).
    float gravity = 18.0f;           ///< Downward acceleration (units/s^2).
    float acceleration = 10.0f;      ///< Exponential speed ease toward the target.
    float groundY = 0.0f;            ///< Fallback plane when no probe hits.
    float groundProbeUpOffset = 2.0f; ///< How far above the feet the probe starts.
    float groundProbeLength = 4.0f;   ///< How far the probe reaches downward.
};

/**
 * @brief Horizontal movement, turning, gravity and jumping for one character.
 *
 * A pure logic component: the caller feeds input axes and a target node each
 * frame, the motor moves the node, turns it toward travel and reports state
 * (speed / grounded / velocity) that drives animation. Grounding uses a
 * probe — by default a downward world-space ray through the scene's
 * colliders — so characters follow terrain instead of a fixed plane.
 *
 * The probe is injectable (SetGroundProbe) so tests and alternate ground
 * sources (e.g. a water surface) can supply heights without a scene.
 */
class CENGINE_API CharacterMotor {
public:
    /**
     * Ground height probe: cast from @p origin along @p down and report the
     * hit's world Y in @p outY.
     * @return true when a surface was hit (no hit = fall back to groundY).
     */
    using GroundProbe = std::function<bool(Vector3 origin, Vector3 down,
                                           float maxDistance, float& outY)>;

    void SetConfig(const CharacterMotorConfig& config) noexcept { m_config = config; }
    const CharacterMotorConfig& Config() const noexcept { return m_config; }

    /** Installs the ground query; an empty probe falls back to `groundY`. */
    void SetGroundProbe(GroundProbe probe) { m_groundProbe = std::move(probe); }

    /** Feeds the stick/wasd axes, each in [-1, 1]; @p running boosts the target speed. */
    void SetInput(float forwardAxis, float rightAxis, bool running) noexcept;

    /**
     * Sets the yaw (degrees) of the input reference frame — the direction
     * "forward" means, normally the camera's yaw. Input axes are resolved
     * against it; the body's own yaw turns toward travel independently.
     */
    void SetReferenceYaw(float yawDegrees) noexcept { m_referenceYaw = yawDegrees; }

    /** Queues a jump; fires only while grounded (and jumpHeight > 0). */
    void Jump() noexcept { m_jumpQueued = true; }

    /**
     * @brief Advances the motor by @p deltaTime, moving and turning @p body.
     *
     * The body's world position is the character's feet. Horizontal motion
     * eases toward the input's target speed; the body yaw turns toward travel
     * at the configured rate; gravity and ground probing handle the vertical
     * axis. Writes the body's position and rotation (yaw only).
     */
    void Update(float deltaTime, Object::Node& body);

    /** True while the feet rest on a surface (not mid-jump/fall). */
    bool IsGrounded() const noexcept { return m_grounded; }

    /** Current horizontal ground speed in units/second. */
    float Speed() const noexcept { return m_speed; }

    /** Current world-space velocity (horizontal travel plus vertical motion). */
    Vector3 Velocity() const noexcept;

    /** The body's facing yaw in degrees. */
    float BodyYaw() const noexcept { return m_bodyYaw; }

private:
    void UpdateVertical(float deltaTime, float& outFeetY);

    CharacterMotorConfig m_config;
    GroundProbe m_groundProbe;
    Vector3 m_feet{};
    Vector3 m_moveDir{0.0f, 0.0f, 1.0f};
    float m_bodyYaw = 0.0f;
    float m_speed = 0.0f;
    float m_verticalVelocity = 0.0f;
    bool m_grounded = true;
    bool m_jumpQueued = false;
    float m_inputForward = 0.0f;
    float m_inputRight = 0.0f;
    bool m_running = false;
    float m_referenceYaw = 0.0f;
};

} // namespace Concord::Gameplay

#endif // CONCORD_CHARACTERMOTOR_H
