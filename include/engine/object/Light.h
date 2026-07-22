#ifndef CONCORD_LIGHT_H
#define CONCORD_LIGHT_H

#include "Concord/CExport.h"
#include "engine/object/LightDesc.h"
#include "engine/object/Node.h"
#include "engine/render/frame/RenderLight.h"
#include "math/Vector3.h"

#include <cstdint>
#include <vector>

namespace Concord::Object {

/**
 * A scene light source: the first-class object that replaces the backend's
 * old single hardcoded directional light.
 *
 * Created through Scene::Spawn<Light>(desc). Its world position and the world
 * direction it emits along are taken from the inherited Node transform each
 * frame, so moving or reparenting the light (or an ancestor) relights the
 * scene next frame with no explicit push — exactly like Box does for geometry.
 * The Scene gathers every active Light into the frame's light list and hands
 * it to the render thread, which feeds the shading pass.
 */
class CENGINE_API Light : public Node {
public:
    explicit Light(LightDesc desc = {});

    LightType Type() const noexcept { return m_type; }
    std::uint32_t Color() const noexcept { return m_color; }
    float Intensity() const noexcept { return m_intensity; }
    float Range() const noexcept { return m_range; }
    float SourceRadius() const noexcept { return m_sourceRadius; }

    /** Returns the directional emitter's apparent angular radius in degrees. */
    float DirectionalAngularRadiusDegrees() const noexcept
    {
        return m_directionalAngularRadiusDegrees;
    }
    const Vector3& Direction() const noexcept { return m_direction; }
    float InnerAngleDegrees() const noexcept { return m_innerAngleDegrees; }
    float OuterAngleDegrees() const noexcept { return m_outerAngleDegrees; }

    /** Switches the emission model (directional/point/spot). */
    void SetType(LightType type);

    /** Sets the light's local forward (the axis rays/cone follow before rotation). */
    void SetDirection(Vector3 direction);

    /** Sets the emitted color, packed 0xRRGGBBAA. */
    void SetColor(std::uint32_t color);

    /** Sets the radiant scale on the color. */
    void SetIntensity(float intensity);

    /** Sets the point/spot fade distance in world units. */
    void SetRange(float range);

    /** Sets the physical emitter size (point/spot); softens wall isophotes. */
    void SetSourceRadius(float radius) noexcept;

    /**
     * Sets the apparent radius of a directional emitter in degrees.
     * Values are clamped to [0, 45]; ignored by point and spot lights.
     */
    void SetDirectionalAngularRadiusDegrees(float radiusDegrees) noexcept;

    /** Sets the spot cone half-angles in degrees (inner is clamped to <= outer). */
    void SetConeAngles(float innerDegrees, float outerDegrees);

    /**
     * Whether this light enters the directional-light shadow pass. Only
     * honored for a directional light; the render backend picks the first one
     * with this flag set as the frame's single shadow caster.
     */
    bool CastShadow() const noexcept { return m_castShadow; }

    /** Toggles this light's participation in the directional-light shadow pass. */
    void SetCastShadow(bool castShadow) noexcept { m_castShadow = castShadow; }

protected:
    /** Tags this directional light as the atmosphere's Sun and configures its disk. */
    void SetSunAppearance(bool sun, bool visibleDisk, float diskIntensity) noexcept;

private:
    void CollectLights(std::vector<RenderLight>& out) const override;

    LightType m_type;
    Vector3 m_direction;
    std::uint32_t m_color;
    float m_intensity;
    float m_range;
    float m_sourceRadius;
    float m_directionalAngularRadiusDegrees;
    float m_innerAngleDegrees;
    float m_outerAngleDegrees;
    bool m_castShadow;
    bool m_sun = false;
    bool m_visibleDisk = false;
    float m_visibleDiskIntensity = 1.0f;
};

} // namespace Concord::Object

#endif // CONCORD_LIGHT_H
