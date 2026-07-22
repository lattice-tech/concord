#ifndef CONCORD_GPULIGHT_H
#define CONCORD_GPULIGHT_H

#include "engine/render/frame/RenderLight.h"

#include <cmath>
#include <cstdint>

namespace Concord {

/**
 * GPU-facing packing of one light for the Forward+ light buffer: four `vec4`s
 * (16 floats) laid out for a storage buffer / data texture the cull compute and
 * the mesh fragment shader both read. Replaces the fixed `vec4[8]` uniform
 * arrays of the classic forward path.
 *
 * Layout (matches the shader `struct GpuLight`):
 *   positionType   = (worldPos.xyz, type)          type: 0 dir, 1 point, 2 spot
 *   directionRange = (dir.xyz, range)
 *   colorIntensity = (colorLinear.rgb, intensity)
 *   spot           = (cosInner, cosOuter, sourceRadius, unused)
 * Colors are unpacked from sRGB to linear here so the shader loop needs no
 * per-light conversion.
 */
struct GpuLight {
    float positionType[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float directionRange[4]{0.0f, -1.0f, 0.0f, 20.0f};
    float colorIntensity[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float spot[4]{1.0f, 0.9f, 0.4f, 0.0f};

    /** Packs a resolved RenderLight into the GPU layout (sRGB → linear color). */
    static GpuLight Pack(const RenderLight& light) noexcept
    {
        const float kPi = 3.14159265358979323846f;
        const auto srgbToLinear = [](float c) {
            return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        };
        GpuLight out;
        out.positionType[0] = light.position[0];
        out.positionType[1] = light.position[1];
        out.positionType[2] = light.position[2];
        out.positionType[3] = static_cast<float>(static_cast<int>(light.type));
        out.directionRange[0] = light.direction[0];
        out.directionRange[1] = light.direction[1];
        out.directionRange[2] = light.direction[2];
        out.directionRange[3] = light.range;
        out.colorIntensity[0] = srgbToLinear(static_cast<float>((light.color >> 24) & 0xffu) / 255.0f);
        out.colorIntensity[1] = srgbToLinear(static_cast<float>((light.color >> 16) & 0xffu) / 255.0f);
        out.colorIntensity[2] = srgbToLinear(static_cast<float>((light.color >> 8) & 0xffu) / 255.0f);
        out.colorIntensity[3] = light.intensity;
        out.spot[0] = std::cos(light.innerAngleDegrees * kPi / 180.0f);
        out.spot[1] = std::cos(light.outerAngleDegrees * kPi / 180.0f);
        out.spot[2] = light.sourceRadius;
        out.spot[3] = 0.0f;
        return out;
    }
};

} // namespace Concord

#endif // CONCORD_GPULIGHT_H
