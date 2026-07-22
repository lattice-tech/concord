#ifndef CONCORD_REFLECTIONMODE_H
#define CONCORD_REFLECTIONMODE_H

#include <cstdint>

namespace Concord::Object {

/**
 * Selects how a renderable node obtains scene reflections.
 *
 * The mode belongs to the node rather than a particular primitive shape, so
 * built-in primitives, imported rigid models and skinned models use the same
 * API. Material parameters still control tint, metallic response and
 * roughness after the reflection source has been selected.
 */
enum class ReflectionMode : std::uint8_t {
    /** Use the ordinary material path without a dynamic scene-capture probe. */
    Standard = 0,

    /** Sample the window's live HDR scene capture on the node's actual mesh. */
    RealtimeScene,
};

} // namespace Concord::Object

#endif // CONCORD_REFLECTIONMODE_H
