#ifndef CONCORD_WATERSURFACESTATE_H
#define CONCORD_WATERSURFACESTATE_H

#include "engine/water/WaterSurfaceDesc.h"

#include <cstdint>

namespace Concord::Water {

/** Which runtime wave source currently drives the surface. */
enum class WaterWaveSource : std::uint8_t {
    /** No animated displacement; the surface is a flat mirror plane. */
    Flat = 0,
    /** Legacy authored Gerstner octaves drive the runtime surface. */
    Gerstner = 1,
    /** The wind-driven spectrum drives the bake and CPU approximation. */
    WindSpectrum = 2,
};

/** Runtime-only state resolved from a WaterSurfaceDesc for one live surface. */
struct WaterSurfaceState {
    /** Seconds of animation accumulated by the node. */
    float time = 0.0f;

    /** Accumulated local-space current offset used by detail layers. */
    float flowOffsetX = 0.0f;
    float flowOffsetZ = 0.0f;

    /** Wind-driven spectrum parameters used when waveSource == WindSpectrum. */
    WindState wind{};

    /** Runtime optical tuning consumed by the fragment shader. */
    WaterOptics optics{};

    /** The resolved wave source for the current frame. */
    WaterWaveSource waveSource = WaterWaveSource::Gerstner;
};

} // namespace Concord::Water

#endif // CONCORD_WATERSURFACESTATE_H
