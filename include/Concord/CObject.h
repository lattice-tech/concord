#ifndef CONCORD_COBJECT_H
#define CONCORD_COBJECT_H

/**
 * Public entry point for renderable object primitives.
 *
 * This header only re-exports the real declarations from the engine
 * module's private headers (see AGENTS.md §3, facade re-export pattern);
 * application code includes this file, never the ones under `engine/`.
 */

#include "engine/object/Box.h"
#include "engine/object/BoxDesc.h"
#include "engine/object/Model.h"
#include "engine/object/ModelDesc.h"
#include "engine/object/Node.h"
#include "engine/object/ObjectId.h"
#include "engine/object/PrimitiveShape.h"
#include "engine/object/ReflectionMode.h"
#include "engine/object/SkinnedModel.h"
#include "engine/object/SkinnedModelDesc.h"
#include "engine/object/Sprite.h"
#include "engine/object/SpriteDesc.h"
#include "engine/object/Transform.h"

#endif // CONCORD_COBJECT_H
