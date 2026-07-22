#ifndef CONCORD_CCOLLISION_H
#define CONCORD_CCOLLISION_H

/**
 * Public entry point for the collision system.
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file, never the ones under `engine/`.
 *
 * Spawn a Concord::Object::Collider into a scene, give it a
 * Concord::Collision::CollisionShape, parent it to the object it guards, and
 * hook OnEnter / OnExit for overlap notifications.
 */

#include "engine/collision/Aabb.h"
#include "engine/collision/CollisionShape.h"
#include "engine/collision/ShapeType.h"
#include "engine/object/Collider.h"
#include "engine/object/ColliderDesc.h"

#endif // CONCORD_CCOLLISION_H
