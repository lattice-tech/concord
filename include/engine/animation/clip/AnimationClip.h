#ifndef CONCORD_ANIMATIONCLIP_H
#define CONCORD_ANIMATIONCLIP_H

#include "engine/animation/clip/AnimationTrack.h"
#include "engine/animation/blend/Pose.h"
#include "engine/animation/clip/SkeletalEventTrack.h"
#include "engine/animation/clip/SpriteTrack.h"
#include "engine/animation/clip/SyncTrack.h"
#include "engine/motion/Easing.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

#include <algorithm>
#include <string>

namespace Concord::Animation {

/**
 * A reusable, target-agnostic animation: keyed transform channels (3D) and/or
 * a sprite frame sequence (2D), sampled by an AnimationPlayer onto a node.
 *
 * A clip owns no runtime state — no current time, no target — so one clip can
 * drive many players at once (every walking enemy shares one "walk" clip).
 * Any subset of channels may be keyed: an empty track means "this player does
 * not touch that part of the target's transform", so a clip that only rotates
 * leaves position and scale alone.
 *
 * This is the data layer beneath the animation state machine (see TODO.md
 * animation roadmap): states reference clips, transitions cross-fade between
 * their sampled poses. Build a clip procedurally with the AddKey helpers, or
 * (later) load one from a model file's animation tracks.
 */
struct AnimationClip {
    /** Human-readable name, handy for state machines and debugging. */
    std::string name;

    /** Local-space translation channel (empty = position not animated). */
    AnimationTrack<Vector3> position;

    /** Local-space rotation channel (empty = rotation not animated). */
    AnimationTrack<Quaternion> rotation;

    /** Local-space scale channel (empty = scale not animated). */
    AnimationTrack<Vector3> scale;

    /** 2D sprite frame sequence (empty = no sprite animation). */
    SpriteTrack sprite;

    /**
     * Named timeline markers fired as playback crosses them (footsteps,
     * impacts, state hooks). Pure data; SkeletalEventSampler delivers them.
     */
    SkeletalEventTrack events;

    /** Named alignment markers for blend spaces and crossfades (SyncTrack). */
    SyncTrack sync;

    /**
     * Explicit clip length in seconds. When 0, Duration() falls back to the
     * longest keyed channel, so a hand-built clip needs no manual length.
     */
    float length = 0.0f;

    // -- Fluent authoring helpers ----------------------------------------

    AnimationClip& AddPositionKey(float time, Vector3 value, Motion::Easing ease = Motion::Easing::Linear)
    {
        position.AddKey(time, value, ease);
        return *this;
    }
    AnimationClip& AddRotationKey(float time, Quaternion value, Motion::Easing ease = Motion::Easing::Linear)
    {
        rotation.AddKey(time, value, ease);
        return *this;
    }
    AnimationClip& AddScaleKey(float time, Vector3 value, Motion::Easing ease = Motion::Easing::Linear)
    {
        scale.AddKey(time, value, ease);
        return *this;
    }
    AnimationClip& AddSpriteFrame(int index, float duration)
    {
        sprite.AddFrame(index, duration);
        return *this;
    }

    /** Effective clip length: the explicit `length`, or the longest channel. */
    float Duration() const noexcept
    {
        float d = length;
        d = std::max(d, position.Duration());
        d = std::max(d, rotation.Duration());
        d = std::max(d, scale.Duration());
        d = std::max(d, sprite.Duration());
        return d;
    }

    /**
     * Samples every keyed channel at @p time into a Pose. Unkeyed channels are
     * left absent (their `has*` flag stays false) so a consumer blends and
     * writes only what this clip animates. Used by the state machine to
     * crossfade and by blend spaces to mix; the player uses the tracks directly.
     */
    Pose SamplePose(float time) const
    {
        Pose pose;
        if (!position.Empty()) {
            pose.position = position.Sample(time);
            pose.hasPosition = true;
        }
        if (!rotation.Empty()) {
            pose.rotation = rotation.Sample(time);
            pose.hasRotation = true;
        }
        if (!scale.Empty()) {
            pose.scale = scale.Sample(time);
            pose.hasScale = true;
        }
        pose.spriteFrame = sprite.Empty() ? -1 : sprite.Sample(time);
        return pose;
    }
};

} // namespace Concord::Animation

#endif // CONCORD_ANIMATIONCLIP_H
