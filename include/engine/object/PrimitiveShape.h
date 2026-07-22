#ifndef CONCORD_PRIMITIVESHAPE_H
#define CONCORD_PRIMITIVESHAPE_H

namespace Concord::Object {

/**
 * Which built-in mesh an object is drawn with.
 *
 * Each value maps to a generator in Concord::Primitives; the engine uploads
 * that mesh once and shares it across every object of the same shape. New
 * shapes are added here and to the loop's shape-to-mesh mapping without
 * touching the object API.
 */
enum class PrimitiveShape {
    Cube,
    Sphere,
    Cylinder,
    Cone,
    Quad,
    Capsule,
    Torus,
};

/** Canonical, human-readable name of a PrimitiveShape (never null). */
inline const char* ToString(PrimitiveShape shape)
{
    switch (shape) {
        case PrimitiveShape::Cube:     return "Cube";
        case PrimitiveShape::Sphere:   return "Sphere";
        case PrimitiveShape::Cylinder: return "Cylinder";
        case PrimitiveShape::Cone:     return "Cone";
        case PrimitiveShape::Quad:     return "Quad";
        case PrimitiveShape::Capsule:  return "Capsule";
        case PrimitiveShape::Torus:    return "Torus";
    }
    return "Cube";
}

} // namespace Concord::Object

#endif // CONCORD_PRIMITIVESHAPE_H
