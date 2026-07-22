#ifndef CONCORD_GAMECONFIG_H
#define CONCORD_GAMECONFIG_H

#include "engine/app/RuntimeMode.h"
#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/render/backend/RenderBackendType.h"

namespace Concord {

/**
 * Describes how Game should bring the engine up on construction.
 *
 * Rendering is always part of the engine lifecycle (it is a game engine),
 * so there is no on/off flag; the fields below only tune it. New fields are
 * added here without breaking existing callers, since every one of them
 * carries a default value.
 */
struct GameConfig {
    /** Graphics API (Vulkan only; Auto resolves to Vulkan). */
    RenderBackendType renderBackend = RenderBackendType::Vulkan;

    /** Runtime profile the engine starts in. */
    RuntimeMode mode = RuntimeMode::Debug;

    /** Full-scene anti-aliasing technique, applied to every window. Default FXAA. */
    AntiAliasing antialiasing = AntiAliasing::Fxaa;
};

} // namespace Concord

#endif // CONCORD_GAMECONFIG_H
