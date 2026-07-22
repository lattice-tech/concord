#ifndef CONCORD_RENDERENVIRONMENT_H
#define CONCORD_RENDERENVIRONMENT_H

#include "engine/environment/EnvironmentSettings.h"

namespace Concord {

/** Cloud displacement resolved for one world snapshot. */
struct RenderCloudAnimation {
    /** Horizontal weather/noise displacement in kilometers. */
    float offsetEastKm = 0.0f;
    /** Horizontal weather/noise displacement in kilometers. */
    float offsetNorthKm = 0.0f;
};

/**
 * Getter-only, self-contained environment value published with a world frame.
 *
 * Resolution sanitizes every authored value and derives animation coordinates
 * solely from absolute simulation time and wind. Consumers do not need mutable
 * environment simulation state.
 */
class CENGINE_API RenderEnvironment final {
public:
    /** Constructs the sanitized default environment. */
    RenderEnvironment() noexcept;

    /** Returns the resolved visible sky controls. */
    const SkyEnvironment& Sky() const noexcept { return m_settings.sky; }
    /** Returns the resolved cloud controls. */
    const VolumetricCloudSettings& Clouds() const noexcept { return m_settings.clouds; }
    /** Returns the resolved global height-fog controls. */
    const HeightFogSettings& HeightFog() const noexcept { return m_settings.heightFog; }
    /** Returns the resolved deterministic timeline. */
    const EnvironmentTimeSettings& Time() const noexcept { return m_settings.time; }
    /** Returns cloud coordinates derived from absolute simulation time. */
    const RenderCloudAnimation& CloudAnimation() const noexcept { return m_cloudAnimation; }

private:
    friend RenderEnvironment ResolveRenderEnvironment(EnvironmentSettings settings) noexcept;

    explicit RenderEnvironment(EnvironmentSettings settings) noexcept;

    EnvironmentSettings m_settings{};
    RenderCloudAnimation m_cloudAnimation{};
};

/** Resolves authored settings into an immutable snapshot-ready value. */
CENGINE_API RenderEnvironment ResolveRenderEnvironment(EnvironmentSettings settings) noexcept;

} // namespace Concord

#endif // CONCORD_RENDERENVIRONMENT_H
