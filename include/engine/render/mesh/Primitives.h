#ifndef CONCORD_PRIMITIVES_H
#define CONCORD_PRIMITIVES_H

#include "engine/render/mesh/MeshData.h"

namespace Concord::Primitives {

/**
 * Built-in mesh generators.
 *
 * Every primitive is returned as plain MeshData sized to fit the unit range
 * (±1 on each axis), matching the unit-cube convention: the caller uploads it
 * once through IRenderBackend::CreateMesh, and an object's `size` then scales
 * it to full world extent. None of these touch a graphics API, so they can be
 * generated and unit-tested on their own.
 */

/** The unit cube (±1 on each axis); the default shape every Box draws. */
MeshData UnitCube();

/** A camera-facing unit quad in the XY plane, spanning +/-1 with full UVs. */
MeshData UnitQuad();

/**
 * A UV sphere of radius 1.
 * @param rings Latitude subdivisions (clamped to >= 2); higher is smoother.
 * @param segments Longitude subdivisions (clamped to >= 3); higher is smoother.
 *
 * The defaults (64 rings × 96 segments) keep the silhouette smooth at common
 * viewing distances and resolutions: at 1080p a unit sphere a few meters away
 * subtends roughly 100–200 px, so each equator facet spans only a handful of
 * pixels, which — combined with the shader's specular anti-aliasing and the
 * engine's post-process AA — reads as a clean circle rather than a polygon.
 * The resulting ~6 300 vertices / ~12 300 triangles are negligible for a
 * shared, instanced mesh uploaded once per shape.
 */
MeshData Sphere(int rings = 64, int segments = 96);

/**
 * A cylinder of radius 1 spanning y in [-1, 1], with flat caps.
 * @param segments Sides around the axis (clamped to >= 3). A low count yields
 *        a prism, i.e. an extruded regular polygon.
 */
MeshData Cylinder(int segments = 24);

/**
 * A cone of base radius 1 at y = -1 rising to an apex at y = 1, with a flat base.
 * @param segments Sides around the base (clamped to >= 3); low counts give a pyramid.
 */
MeshData Cone(int segments = 24);

/**
 * A Y-axis capsule with radius 0.5 and a unit-long cylindrical section.
 * @param hemisphereRings Subdivisions from each pole to its cylinder seam.
 * @param segments Sides around the Y axis.
 */
MeshData Capsule(int hemisphereRings = 12, int segments = 32);

/**
 * A Y-axis torus with major radius 0.7 and tube radius 0.3.
 * @param majorSegments Subdivisions around the central ring.
 * @param minorSegments Subdivisions around the tube.
 */
MeshData Torus(int majorSegments = 48, int minorSegments = 16);

} // namespace Concord::Primitives

#endif // CONCORD_PRIMITIVES_H
