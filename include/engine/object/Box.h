#ifndef CONCORD_BOX_H
#define CONCORD_BOX_H

#include "Concord/CExport.h"
#include "engine/material/MaterialDesc.h"
#include "engine/object/BoxDesc.h"
#include "engine/object/Node.h"
#include "engine/object/PrimitiveShape.h"
#include "engine/render/frame/RenderInstance.h"
#include "math/Vector3.h"

#include <cstdint>
#include <vector>

namespace Concord::Object {

/**
 * A renderable primitive node (cube by default; see BoxDesc::shape).
 *
 * Created through Scene::Spawn<Box>(desc). Its placement lives in the Node
 * transform it inherits — use SetPosition / Translate / Rotate / SetScale (and
 * SetParent to attach it under another node); `size` here is only the
 * primitive's own dimensions, applied on top of the node's world transform.
 * The scene collects its draw each frame while active, so transform edits (its
 * own or an ancestor's) show up next frame with no explicit push.
 */
class CENGINE_API Box : public Node {
public:
    explicit Box(BoxDesc desc = {});

    PrimitiveShape Shape() const noexcept { return m_shape; }
    const Vector3& Size() const noexcept { return m_size; }

    /** The current base color (the material's albedo), packed 0xRRGGBBAA. */
    std::uint32_t Color() const noexcept { return m_material.surface.albedo; }

    /** The full surface description currently applied. */
    const Material::MaterialDesc& GetMaterial() const noexcept { return m_material; }

    /** Sets the primitive's dimensions (full extent per axis). */
    void SetSize(Vector3 size);

    /** Sets just the base color (the material's albedo), packed 0xRRGGBBAA. */
    void SetColor(std::uint32_t color);

    /**
     * Replaces the whole surface description, e.g.
     * `SetMaterial({.surface = {.albedo = COLOR_GOLD, .metallic = 1.0f, .roughness = 0.25f}})`.
     * Replace-wholesale semantics: unnamed fields take Material::MaterialDesc's
     * defaults (copy from GetMaterial() first to keep the current values).
     */
    void SetMaterial(Material::MaterialDesc material);

    /** Switches which built-in mesh is drawn. */
    void SetShape(PrimitiveShape shape);

private:
    void CollectRender(std::vector<RenderInstance>& out) const override;

    PrimitiveShape m_shape;
    Vector3 m_size;
    Material::MaterialDesc m_material;
};

} // namespace Concord::Object

#endif // CONCORD_BOX_H
