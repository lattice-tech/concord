#ifndef CONCORD_CANIMATION_H
#define CONCORD_CANIMATION_H

/**
 * Public entry point for the data-driven animation system.
 *
 * Re-exports the real declarations from the animation module (facade pattern,
 * AGENTS.md §3): keyed transform/sprite clips (AnimationClip) sampled onto a
 * scene node by an AnimationPlayer, plus the playback-mode enum. This is the
 * clip layer (L2 of the animation roadmap); the state machine that blends
 * between clips builds on top of it.
 *
 *     // 3D: a box that hops and spins, looping.
 *     Concord::Animation::AnimationClip hop;
 *     hop.AddPositionKey(0.0f, {0, 0, 0})
 *        .AddPositionKey(0.5f, {0, 2, 0}, Concord::Motion::Easing::OutQuad)
 *        .AddPositionKey(1.0f, {0, 0, 0}, Concord::Motion::Easing::OutBounce)
 *        .AddRotationKey(0.0f, Concord::Quaternion::FromEuler({.yaw = 0}))
 *        .AddRotationKey(1.0f, Concord::Quaternion::FromEuler({.yaw = 360}));
 *
 *     Concord::Animation::AnimationPlayer player;
 *     player.SetTarget(&box);
 *     player.Play(&hop, Concord::Animation::PlaybackMode::Loop);
 *     box.OnUpdate([&](float dt){ player.Update(dt); });
 *
 *     // 2D: a sprite frame sequence; CurrentFrame() gives the active cell.
 *     Concord::Animation::AnimationClip walk;
 *     walk.AddSpriteFrame(0, 0.1f).AddSpriteFrame(1, 0.1f).AddSpriteFrame(2, 0.1f);
 */
#include "engine/animation/AnimStateMachine.h"
#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationParameters.h"
#include "engine/animation/AnimationPlayer.h"
#include "engine/animation/AnimationState.h"
#include "engine/animation/AnimationTrack.h"
#include "engine/animation/AnimationTransition.h"
#include "engine/animation/BlendSpace1D.h"
#include "engine/animation/PlaybackMode.h"
#include "engine/animation/Pose.h"
#include "engine/animation/SkeletalBlend.h"
#include "engine/animation/SkeletalClip.h"
#include "engine/animation/SkeletalState.h"
#include "engine/animation/SkeletalStateMachine.h"
#include "engine/animation/Skeleton.h"
#include "engine/animation/SpriteTrack.h"

#endif // CONCORD_CANIMATION_H
