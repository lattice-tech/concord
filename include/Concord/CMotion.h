#ifndef CONCORD_CMOTION_H
#define CONCORD_CMOTION_H

/**
 * Public entry point for the Object motion system.
 *
 * Re-exports the motion module's real declarations (facade pattern, AGENTS.md
 * §3): easing curves and the Mover that drives an Object::Node's transform over
 * time. This is the actuator layer the animation state machine builds on.
 */
#include "engine/motion/Easing.h"
#include "engine/motion/Mover.h"

#endif // CONCORD_CMOTION_H
