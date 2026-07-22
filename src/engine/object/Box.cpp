#include "engine/object/Box.h"

#include <bx/math.h>

namespace Concord::Object {

namespace {

/** out = lhs * rhs (column-vector order); bx::mtxMul(out, a, b) yields b * a, so the args are swapped. */
void Multiply(float* out, const float* lhs, const float* rhs) noexcept
{
    bx::mtxMul(out, rhs, lhs);
}

} // namespace

Box::Box(BoxDesc desc)
    : m_shape(desc.shape)
    , m_size(desc.size)
    , m_material(desc.material)
{
    // `color` is the shorthand for a flat albedo: apply it unless the material
    // block already carried an explicit (non-white) albedo, which then wins.
    if (m_material.surface.albedo == COLOR_WHITE) {
        m_material.surface.albedo = desc.color;
    }
    SetLocalTransform(desc.transform);
}

void Box::SetSize(Vector3 size)
{
    m_size = size;
}

void Box::SetColor(std::uint32_t color)
{
    m_material.surface.albedo = color;
}

void Box::SetMaterial(Material::MaterialDesc material)
{
    m_material = material;
}

void Box::SetShape(PrimitiveShape shape)
{
    m_shape = shape;
}

void Box::CollectRender(std::vector<RenderInstance>& out) const
{
    // Render matrix = node world transform * scale(size/2): the built-in meshes
    // span +/-1, so half the size scales the unit primitive to full extent.
    float sizeScale[16];
    bx::mtxScale(sizeScale, m_size.x * 0.5f, m_size.y * 0.5f, m_size.z * 0.5f);

    RenderInstance instance;
    Multiply(instance.world, WorldMatrix(), sizeScale); // nodeWorld * scale(size/2)
    instance.material = ResolveMaterial(m_material);
    instance.material.reflectivity *= Reflectivity();
    instance.shape = m_shape;
    instance.rayTraced = UsesRealtimeReflection();
    instance.reflectionOwner = instance.rayTraced ? ReflectionOwnerKey() : 0;
    out.push_back(instance);
}

} // namespace Concord::Object
