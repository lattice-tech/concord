#ifndef CONCORD_GPULIGHT_H
#define CONCORD_GPULIGHT_H

#include "engine/render/frame/RenderLight.h"

#include <cmath>
#include <cstdint>

namespace Concord {

/**
 * GPU-facing packing of one light for the Forward+ light buffer: four `vec4`s
 * (16 floats) laid out as four data-texture texels read by the cull compute and
 * mesh fragment shaders.
 *
 * Texture-row layout:
 *   positionType   = (worldPos.xyz, type)          type: 0 dir, 1 point, 2 spot
 *   directionRange = (dir.xyz, range)
 *   colorIntensity = (colorLinear.rgb, intensity)
 *   spot           = (cosInner, cosOuter, sourceRadius, source light index)
 * Colors are unpacked from sRGB to linear here so the shader loop needs no
 * per-light conversion.
 */
struct GpuLight {
    float positionType[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float directionRange[4]{0.0f, -1.0f, 0.0f, 20.0f};
    float colorIntensity[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float spot[4]{1.0f, 0.9f, 0.4f, 0.0f};

    /**
     * Packs a resolved light and its index in the render-view light array.
     * The index lets clustered shading identify the directional shadow caster
     * after the culler reorders directional lights ahead of local lights.
     */
    static GpuLight Pack(const RenderLight& light, std::uint32_t sourceIndex = 0) noexcept
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
        out.spot[3] = static_cast<float>(sourceIndex);
        return out;
    }
};

} // namespace Concord

#endif // CONCORD_GPULIGHT_H
