#ifndef CONCORD_CHARACTERCONFIG_H
#define CONCORD_CHARACTERCONFIG_H

#include "math/Vector3.h"

#include <string>

namespace Concord::Gameplay {

/** How the camera a Character drives is framed. */
enum class CameraStyle {
    ThirdPerson, ///< Camera trails behind and above, so the body is visible.
    FirstPerson, ///< Camera sits at the character's eyes.
};

/**
 * Everything a Character is built from — a plain aggregate so a caller names
 * only what it needs, e.g. `{.model = "hero.glb", .position = {0, 0, 0}}`.
 *
 * The defaults describe a human-scale runner using the clip names most rigged
 * glTF characters ship with (Idle/Walking/Running/Jump); point `model` at a
 * rigged file and it works. Tune speeds/jump/camera to taste, or set
 * `modelYawOffsetDegrees` if the imported model faces a different axis than the
 * engine's +Z forward.
 */
struct CharacterConfig {
    /** Rigged glTF/GLB with a skin and locomotion clips. */
    std::string model;

    /** Where the character's feet start, in world space. */
    Vector3 position{0.0f, 0.0f, 0.0f};

    /** Uniform model scale (imported meshes are used as authored otherwise). */
    float scale = 1.0f;

    /** World Y the feet rest on; the character never falls below it. */
    float groundY = 0.0f;

    /** Clip names looked up in the model by name. */
    std::string idleClip = "Idle";
    std::string walkClip = "Walking";
    std::string runClip = "Running";
    std::string jumpClip = "Jump";

    float walkSpeed = 2.4f;              ///< Ground speed when walking (units/s).
    float runSpeed = 6.0f;               ///< Ground speed when running (units/s).
    float turnSpeedDegrees = 720.0f;     ///< How fast the body turns to face travel.
    float jumpHeight = 1.6f;             ///< Peak jump height (units).
    float gravity = 18.0f;               ///< Downward acceleration (units/s^2).
    float modelYawOffsetDegrees = 0.0f;  ///< Added to facing if the model's front isn't +Z.

    CameraStyle cameraStyle = CameraStyle::ThirdPerson;
    float cameraDistance = 5.0f;         ///< Third-person trail distance.
    float cameraTargetHeight = 1.6f;     ///< Look-at height above the feet (third person).
    float eyeHeight = 1.6f;              ///< Camera height above feet (first person).
    float mouseSensitivity = 0.12f;      ///< Degrees of look per pixel of mouse motion.
};

} // namespace Concord::Gameplay

#endif // CONCORD_CHARACTERCONFIG_H
