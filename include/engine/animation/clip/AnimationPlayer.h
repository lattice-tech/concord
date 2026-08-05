#ifndef CONCORD_ANIMATIONPLAYER_H
#define CONCORD_ANIMATIONPLAYER_H

#include "Concord/CExport.h"
#include "engine/animation/clip/AnimationClip.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/clip/SkeletalEventSampler.h"

namespace Concord {
namespace Object {
class Node;
}

namespace Animation {

/**
 * Plays an AnimationClip onto a scene node.
 *
 * The player is the runtime half of the data-driven animation path: it holds a
 * playback clock, advances it each Update by `dt * speed`, wraps it per the
 * PlaybackMode, samples the clip's keyed channels and writes them to the
 * target node's *local* transform (so animation composes with the scene
 * hierarchy). Channels the clip does not key are left untouched, letting an
 * animation that only rotates coexist with code that positions the node.
 *
 * Typical use mirrors Motion::Mover — drive it from the node's OnUpdate:
 * ```
 *   player.SetTarget(&node);
 *   player.Play(&walkClip, PlaybackMode::Loop);
 *   node.OnUpdate([&](float dt){ player.Update(dt); });
 * ```
 * The clip and target are referenced, not owned, so both must outlive the
 * player. For 2D, CurrentFrame() exposes the active sprite index for the
 * caller to map onto a texture.
 *
 * This is the L2 clip layer; the animation state machine (L3) will own one or
 * more players per entity and cross-fade between their poses (see the
 * animation roadmap in TODO.md).
 */
class CENGINE_API AnimationPlayer {
public:
    /** Sets the node this player animates (its local transform is written each Update). */
    void SetTarget(Object::Node* node) noexcept { m_target = node; }

    /**
     * Starts playing @p clip from the beginning in @p mode. Passing nullptr is
     * equivalent to Stop(). The clip is referenced, not copied.
     */
    void Play(const AnimationClip* clip, PlaybackMode mode = PlaybackMode::Loop);

    /** Stops playback and forgets the clip; the target keeps its current pose. */
    void Stop() noexcept;

    /** Freezes the clock; the pose holds until Resume(). */
    void Pause() noexcept { m_playing = false; }

    /** Resumes a paused clip (no-op if there is no clip). */
    void Resume() noexcept;

    /** Playback rate multiplier (1 = real time, 2 = double speed, 0.5 = half). */
    void SetSpeed(float speed) noexcept { m_speed = speed; }

    /**
     * Advances the clock by @p deltaTime seconds and writes the sampled pose to
     * the target. A no-op when paused, stopped, or without a clip/target.
     */
    void Update(float deltaTime);

    /** True while a clip is actively advancing (false when paused/stopped/finished-Once). */
    bool IsPlaying() const noexcept { return m_playing; }

    /** Current playback time within the clip, in seconds. */
    float Time() const noexcept { return m_time; }

    /** Active 2D sprite frame index at the current time, or -1 if the clip has none. */
    int CurrentFrame() const;

    /**
     * @brief Registers a callback for the clip's animation-event markers.
     *
     * Markers defined on the clip's `events` track fire as playback crosses
     * them, honouring the playback direction and loop wrapping (see
     * SkeletalEventSampler). Only one callback is stored; setting another
     * replaces it. The callback runs inside Update on the caller's thread.
     */
    void SetEventCallback(std::function<void(const SkeletalEvent&)> callback);

private:
    /** Writes the clip's sampled channels at `m_time` to the target's local transform. */
    void ApplyPose();

    /** Fires the clip's event markers for the frame's [oldTime, newTime] window. */
    void FireEvents(float oldTime, float newTime, float bounceBoundary,
                    bool wasForward, float duration, float rawStep);

    const AnimationClip* m_clip = nullptr;
    Object::Node* m_target = nullptr;
    PlaybackMode m_mode = PlaybackMode::Loop;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_playing = false;
    bool m_pingPongReversing = false; ///< true while a PingPong clip is running backwards
    SkeletalEventSampler m_eventSampler;
};

} // namespace Animation
} // namespace Concord

#endif // CONCORD_ANIMATIONPLAYER_H
