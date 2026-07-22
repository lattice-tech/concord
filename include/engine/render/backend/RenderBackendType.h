#ifndef CONCORD_RENDERBACKENDTYPE_H
#define CONCORD_RENDERBACKENDTYPE_H

namespace Concord {

/**
 * Graphics API a render backend should target.
 *
 * Concord ships **Vulkan only**. `Auto` and any legacy config aliases resolve
 * to Vulkan. DirectX was removed after sustained DX12-specific transform /
 * shading defects; do not reintroduce a second API without a full parity test
 * of instance matrices, uniform lifetime, and sampler bindings.
 */
enum class RenderBackendType {
    Auto,   /**< Same as Vulkan (kept so existing configs keep loading). */
    Vulkan,
};

/** Canonical, human-readable name of a RenderBackendType (never null). */
inline const char* ToString(RenderBackendType type)
{
    switch (type) {
        case RenderBackendType::Vulkan: return "Vulkan";
        case RenderBackendType::Auto:   return "Vulkan"; // Auto == Vulkan
    }
    return "Vulkan";
}

} // namespace Concord

#endif // CONCORD_RENDERBACKENDTYPE_H
