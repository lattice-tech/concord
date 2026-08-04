#ifndef CONCORD_POSE_H
#define CONCORD_POSE_H

#include "engine/animation/AnimationTrack.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

namespace Concord::Animation {

/**
 * A single sampled animation snapshot: the local-space transform an animation
 * wants a node to hold at one instant, plus a 2D sprite frame.
 *
 * The `has*` flags mark which channels the source clip actually keyed, so a
 * consumer (player or state machine) only overwrites the parts of a node's
 * transform that are animated and blends only matching channels. This is the
 * value that crossfades and blend spaces interpolate — clips are sampled into
 * Poses, Poses are blended, and the final Pose is written to the node.
 */
struct Pose {
    Vector3 position{};
    Quaternion rotation{};
    Vector3 scale{1.0f, 1.0f, 1.0f};

    bool hasPosition = false;
    bool hasRotation = false;
    bool hasScale = false;

    /** Active 2D sprite frame index, or -1 when the source has no sprite track. */
    int spriteFrame = -1;
};

/**
 * Blends two poses by weight @p t (0 → a, 1 → b). Position and scale lerp,
 * rotation slerps; a channel is present in the result if either input has it,
 * and when only one side has a channel that side's value is used at full
 * weight (so blending a rotate-only clip against a move-only clip keeps both).
 * The sprite frame snaps to whichever side has the greater weight.
 */
inline Pose BlendPose(const Pose& a, const Pose& b, float t) noexcept
{
    Pose out;

    out.hasPosition = a.hasPosition || b.hasPosition;
    if (a.hasPosition && b.hasPosition) {
        out.position = AnimInterpolate(a.position, b.position, t);
    } else if (a.hasPosition) {
        out.position = a.position;
    } else if (b.hasPosition) {
        out.position = b.position;
    }

    out.hasRotation = a.hasRotation || b.hasRotation;
    if (a.hasRotation && b.hasRotation) {
        out.rotation = AnimInterpolate(a.rotation, b.rotation, t);
    } else if (a.hasRotation) {
        out.rotation = a.rotation;
    } else if (b.hasRotation) {
        out.rotation = b.rotation;
    }

    out.hasScale = a.hasScale || b.hasScale;
    if (a.hasScale && b.hasScale) {
        out.scale = AnimInterpolate(a.scale, b.scale, t);
    } else if (a.hasScale) {
        out.scale = a.scale;
    } else if (b.hasScale) {
        out.scale = b.scale;
    }

    out.spriteFrame = (t < 0.5f) ? a.spriteFrame : b.spriteFrame;
    if (out.spriteFrame < 0) {
        out.spriteFrame = (a.spriteFrame >= 0) ? a.spriteFrame : b.spriteFrame;
    }
    return out;
}

} // namespace Concord::Animation

#endif // CONCORD_POSE_H
