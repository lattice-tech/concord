#ifndef CONCORD_CCHARACTER_H
#define CONCORD_CCHARACTER_H

/**
 * Public entry point for the player-character controller.
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file, never the ones under `engine/`.
 *
 * Spawn one into a scene and drive it with the keyboard/mouse:
 *
 *     Concord::Object::Character& hero = scene.Spawn<Concord::Object::Character>(
 *         Concord::Gameplay::CharacterConfig{
 *             .model = "bin/Assets/models/Soldier.glb",
 *             .idleClip = "Idle", .walkClip = "Walk", .runClip = "Run",
 *         });
 *     // WASD walks, Shift runs, Space jumps, the mouse looks — all internal.
 */

#include "engine/character/Character.h"
#include "engine/character/CharacterConfig.h"

#endif // CONCORD_CCHARACTER_H
