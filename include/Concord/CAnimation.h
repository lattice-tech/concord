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
#include "engine/animation/state/AnimStateMachine.h"
#include "engine/animation/clip/AnimationClip.h"
#include "engine/animation/state/AnimationParameters.h"
#include "engine/animation/clip/AnimationPlayer.h"
#include "engine/animation/state/AnimationState.h"
#include "engine/animation/clip/AnimationTrack.h"
#include "engine/animation/state/AnimationTransition.h"
#include "engine/animation/blend/BlendSpace1D.h"
#include "engine/animation/blend/BlendSpace2D.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/blend/Pose.h"
#include "engine/animation/blend/SkeletalBlend.h"
#include "engine/animation/blend/SkeletalBlendSpace2D.h"
#include "engine/animation/clip/SkeletalClip.h"
#include "engine/animation/clip/SkeletalEventSampler.h"
#include "engine/animation/clip/SkeletalEventTrack.h"
#include "engine/animation/clip/SyncTrack.h"
#include "engine/animation/state/SkeletalState.h"
#include "engine/animation/state/SkeletalStateMachine.h"
#include "engine/animation/skeleton/BoneAttachment.h"
#include "engine/animation/skeleton/Skeleton.h"
#include "engine/animation/skeleton/SkeletonRemapper.h"
#include "engine/animation/layer/SkeletalAnimator.h"
#include "engine/animation/layer/SkeletalLayer.h"
#include "engine/animation/layer/SkeletalLayerMask.h"
#include "engine/animation/clip/SpriteTrack.h"

#endif // CONCORD_CANIMATION_H
