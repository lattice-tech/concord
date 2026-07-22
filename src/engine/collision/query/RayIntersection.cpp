#include "engine/collision/query/RayIntersection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Concord::Collision {

namespace {

struct AffineInverse {
    // Row-major inverse of the world matrix's upper-left 3x3 block.
    double linear[9]{};
    double translation[3]{};
};

bool IsFinite(const Vector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double Dot(const std::array<double, 3>& left, const std::array<double, 3>& right) noexcept
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

bool BuildAffineInverse(const float matrix[16], AffineInverse& out) noexcept
{
    if (matrix == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < 16; ++index) {
        if (!std::isfinite(matrix[index])) {
            return false;
        }
    }

    const double a00 = matrix[0];
    const double a01 = matrix[4];
    const double a02 = matrix[8];
    const double a10 = matrix[1];
    const double a11 = matrix[5];
    const double a12 = matrix[9];
    const double a20 = matrix[2];
    const double a21 = matrix[6];
    const double a22 = matrix[10];
    const double maxCoefficient = std::max({
        std::abs(a00), std::abs(a01), std::abs(a02),
        std::abs(a10), std::abs(a11), std::abs(a12),
        std::abs(a20), std::abs(a21), std::abs(a22)});
    if (maxCoefficient == 0.0) {
        return false;
    }

    const double n00 = a00 / maxCoefficient;
    const double n01 = a01 / maxCoefficient;
    const double n02 = a02 / maxCoefficient;
    const double n10 = a10 / maxCoefficient;
    const double n11 = a11 / maxCoefficient;
    const double n12 = a12 / maxCoefficient;
    const double n20 = a20 / maxCoefficient;
    const double n21 = a21 / maxCoefficient;
    const double n22 = a22 / maxCoefficient;
    const double normalizedDeterminant =
        n00 * (n11 * n22 - n12 * n21)
        - n01 * (n10 * n22 - n12 * n20)
        + n02 * (n10 * n21 - n11 * n20);
    constexpr double kDeterminantTolerance =
        std::numeric_limits<double>::epsilon() * 64.0;
    if (!std::isfinite(normalizedDeterminant)
        || std::abs(normalizedDeterminant) <= kDeterminantTolerance) {
        return false;
    }

    const double inverseScaleDeterminant =
        1.0 / (maxCoefficient * normalizedDeterminant);
    out.linear[0] =  (n11 * n22 - n12 * n21) * inverseScaleDeterminant;
    out.linear[1] =  (n02 * n21 - n01 * n22) * inverseScaleDeterminant;
    out.linear[2] =  (n01 * n12 - n02 * n11) * inverseScaleDeterminant;
    out.linear[3] =  (n12 * n20 - n10 * n22) * inverseScaleDeterminant;
    out.linear[4] =  (n00 * n22 - n02 * n20) * inverseScaleDeterminant;
    out.linear[5] =  (n02 * n10 - n00 * n12) * inverseScaleDeterminant;
    out.linear[6] =  (n10 * n21 - n11 * n20) * inverseScaleDeterminant;
    out.linear[7] =  (n01 * n20 - n00 * n21) * inverseScaleDeterminant;
    out.linear[8] =  (n00 * n11 - n01 * n10) * inverseScaleDeterminant;
    out.translation[0] = matrix[12];
    out.translation[1] = matrix[13];
    out.translation[2] = matrix[14];
    return std::all_of(std::begin(out.linear), std::end(out.linear), [](double value) {
        return std::isfinite(value);
    });
}

std::array<double, 3> TransformPointToLocal(const AffineInverse& inverse,
                                            const Vector3& point) noexcept
{
    const double x = static_cast<double>(point.x) - inverse.translation[0];
    const double y = static_cast<double>(point.y) - inverse.translation[1];
    const double z = static_cast<double>(point.z) - inverse.translation[2];
    return {
        inverse.linear[0] * x + inverse.linear[1] * y + inverse.linear[2] * z,
        inverse.linear[3] * x + inverse.linear[4] * y + inverse.linear[5] * z,
        inverse.linear[6] * x + inverse.linear[7] * y + inverse.linear[8] * z,
    };
}

std::array<double, 3> TransformDirectionToLocal(const AffineInverse& inverse,
                                                const Vector3& direction) noexcept
{
    return {
        inverse.linear[0] * direction.x + inverse.linear[1] * direction.y + inverse.linear[2] * direction.z,
        inverse.linear[3] * direction.x + inverse.linear[4] * direction.y + inverse.linear[5] * direction.z,
        inverse.linear[6] * direction.x + inverse.linear[7] * direction.y + inverse.linear[8] * direction.z,
    };
}

Vector3 TransformNormalToWorld(const AffineInverse& inverse,
                               const std::array<double, 3>& normal,
                               const Vector3& fallback) noexcept
{
    const double x = inverse.linear[0] * normal[0]
        + inverse.linear[3] * normal[1] + inverse.linear[6] * normal[2];
    const double y = inverse.linear[1] * normal[0]
        + inverse.linear[4] * normal[1] + inverse.linear[7] * normal[2];
    const double z = inverse.linear[2] * normal[0]
        + inverse.linear[5] * normal[1] + inverse.linear[8] * normal[2];
    const double lengthSquared = x * x + y * y + z * z;
    if (!std::isfinite(lengthSquared)
        || lengthSquared <= std::numeric_limits<double>::min()) {
        return fallback;
    }
    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    return {
        static_cast<float>(x * inverseLength),
        static_cast<float>(y * inverseLength),
        static_cast<float>(z * inverseLength),
    };
}

bool SetInitialOverlap(const Ray& ray, float distance, ShapeType shape,
                       RaycastHit& outHit) noexcept
{
    const Vector3 position = ray.origin + ray.direction * distance;
    if (!IsFinite(position)) {
        return false;
    }
    outHit.position = position;
    outHit.normal = ray.direction * -1.0f;
    outHit.distance = distance;
    outHit.startedInside = true;
    outHit.shape = shape;
    return true;
}

bool IntersectBox(const Ray& ray, const CollisionShape& shape,
                  const AffineInverse& inverse,
                  const std::array<double, 3>& localOrigin,
                  const std::array<double, 3>& localDirection,
                  float minDistance, float maxDistance,
                  RaycastHit& outHit) noexcept
{
    if (!IsFinite(shape.halfExtents) || !IsFinite(shape.offset)
        || shape.halfExtents.x < 0.0f || shape.halfExtents.y < 0.0f
        || shape.halfExtents.z < 0.0f) {
        return false;
    }

    const std::array<double, 3> center{shape.offset.x, shape.offset.y, shape.offset.z};
    const std::array<double, 3> half{
        shape.halfExtents.x, shape.halfExtents.y, shape.halfExtents.z};
    const std::array<double, 3> segmentStart{
        localOrigin[0] + localDirection[0] * minDistance,
        localOrigin[1] + localDirection[1] * minDistance,
        localOrigin[2] + localDirection[2] * minDistance,
    };
    bool startedInside = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        startedInside = startedInside
            && segmentStart[axis] >= center[axis] - half[axis]
            && segmentStart[axis] <= center[axis] + half[axis];
    }
    if (startedInside) {
        return SetInitialOverlap(ray, minDistance, ShapeType::Box, outHit);
    }

    double nearDistance = -std::numeric_limits<double>::infinity();
    double farDistance = std::numeric_limits<double>::infinity();
    std::array<double, 3> localNormal{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double lower = center[axis] - half[axis];
        const double upper = center[axis] + half[axis];
        const double origin = localOrigin[axis];
        const double direction = localDirection[axis];
        if (std::abs(direction) <= std::numeric_limits<double>::min()) {
            if (origin < lower || origin > upper) {
                return false;
            }
            continue;
        }

        const double first = (lower - origin) / direction;
        const double second = (upper - origin) / direction;
        const double axisNear = std::min(first, second);
        const double axisFar = std::max(first, second);
        if (axisNear > nearDistance) {
            nearDistance = axisNear;
            localNormal = {0.0, 0.0, 0.0};
            localNormal[axis] = direction > 0.0 ? -1.0 : 1.0;
        }
        farDistance = std::min(farDistance, axisFar);
        if (nearDistance > farDistance) {
            return false;
        }
    }

    if (!std::isfinite(nearDistance)
        || nearDistance < minDistance || nearDistance > maxDistance) {
        return false;
    }
    const float distance = static_cast<float>(nearDistance);
    outHit.position = ray.origin + ray.direction * distance;
    outHit.normal = TransformNormalToWorld(inverse, localNormal, ray.direction * -1.0f);
    outHit.distance = distance;
    outHit.startedInside = false;
    outHit.shape = ShapeType::Box;
    return IsFinite(outHit.position) && IsFinite(outHit.normal);
}

bool IntersectSphere(const Ray& ray, const CollisionShape& shape,
                     const AffineInverse& inverse,
                     const std::array<double, 3>& localOrigin,
                     const std::array<double, 3>& localDirection,
                     float minDistance, float maxDistance,
                     RaycastHit& outHit) noexcept
{
    if (!std::isfinite(shape.radius) || shape.radius <= 0.0f || !IsFinite(shape.offset)) {
        return false;
    }
    const std::array<double, 3> center{shape.offset.x, shape.offset.y, shape.offset.z};
    const std::array<double, 3> offset{
        localOrigin[0] - center[0],
        localOrigin[1] - center[1],
        localOrigin[2] - center[2],
    };
    const double radiusSquared = static_cast<double>(shape.radius) * shape.radius;
    const std::array<double, 3> segmentOffset{
        offset[0] + localDirection[0] * minDistance,
        offset[1] + localDirection[1] * minDistance,
        offset[2] + localDirection[2] * minDistance,
    };
    if (Dot(segmentOffset, segmentOffset) <= radiusSquared) {
        return SetInitialOverlap(ray, minDistance, ShapeType::Sphere, outHit);
    }

    const double a = Dot(localDirection, localDirection);
    const double b = Dot(offset, localDirection);
    if (!std::isfinite(a) || a <= std::numeric_limits<double>::min()) {
        return false;
    }

    const double closestDistance = -b / a;
    if (!std::isfinite(closestDistance)) {
        return false;
    }
    const std::array<double, 3> closestOffset{
        std::fma(localDirection[0], closestDistance, offset[0]),
        std::fma(localDirection[1], closestDistance, offset[1]),
        std::fma(localDirection[2], closestDistance, offset[2]),
    };
    const double perpendicularSquared = Dot(closestOffset, closestOffset);
    if (!std::isfinite(perpendicularSquared)) {
        return false;
    }
    double radialSquared = radiusSquared - perpendicularSquared;
    const double radialTolerance = std::numeric_limits<double>::epsilon()
        * std::max({1.0, radiusSquared, perpendicularSquared}) * 16.0;
    if (radialSquared < -radialTolerance) {
        return false;
    }
    radialSquared = std::max(0.0, radialSquared);
    const double halfSpan = std::sqrt(radialSquared / a);
    const double nearDistance = closestDistance - halfSpan;
    const double farDistance = closestDistance + halfSpan;
    double distance = nearDistance;
    if (distance < minDistance) {
        distance = farDistance;
    }
    if (!std::isfinite(distance) || distance < minDistance || distance > maxDistance) {
        return false;
    }

    const std::array<double, 3> localHit{
        offset[0] + localDirection[0] * distance,
        offset[1] + localDirection[1] * distance,
        offset[2] + localDirection[2] * distance,
    };
    outHit.position = ray.origin + ray.direction * static_cast<float>(distance);
    outHit.normal = TransformNormalToWorld(inverse, localHit, ray.direction * -1.0f);
    outHit.distance = static_cast<float>(distance);
    outHit.startedInside = false;
    outHit.shape = ShapeType::Sphere;
    return IsFinite(outHit.position) && IsFinite(outHit.normal);
}

} // namespace

bool NormalizeRay(const Ray& ray, Ray& outNormalized) noexcept
{
    if (!IsFinite(ray.origin) || !IsFinite(ray.direction)) {
        return false;
    }
    const double x = ray.direction.x;
    const double y = ray.direction.y;
    const double z = ray.direction.z;
    const double lengthSquared = x * x + y * y + z * z;
    if (!std::isfinite(lengthSquared)
        || lengthSquared <= std::numeric_limits<double>::min()) {
        return false;
    }
    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    outNormalized.origin = ray.origin;
    outNormalized.direction = {
        static_cast<float>(x * inverseLength),
        static_cast<float>(y * inverseLength),
        static_cast<float>(z * inverseLength),
    };
    return IsFinite(outNormalized.direction);
}

bool IsValidRaycastRange(float minDistance, float maxDistance) noexcept
{
    return std::isfinite(minDistance) && minDistance >= 0.0f
        && !std::isnan(maxDistance) && maxDistance >= minDistance;
}

bool IntersectRayShape(const Ray& normalizedRay,
                       const CollisionShape& shape,
                       const float worldMatrix[16],
                       float minDistance,
                       float maxDistance,
                       RaycastHit& outHit) noexcept
{
    if (!IsValidRaycastRange(minDistance, maxDistance)) {
        return false;
    }
    AffineInverse inverse;
    if (!BuildAffineInverse(worldMatrix, inverse)) {
        return false;
    }
    const std::array<double, 3> localOrigin =
        TransformPointToLocal(inverse, normalizedRay.origin);
    const std::array<double, 3> localDirection =
        TransformDirectionToLocal(inverse, normalizedRay.direction);
    if (!std::all_of(localOrigin.begin(), localOrigin.end(), [](double value) {
            return std::isfinite(value);
        })
        || !std::all_of(localDirection.begin(), localDirection.end(), [](double value) {
            return std::isfinite(value);
        })) {
        return false;
    }

    switch (shape.type) {
        case ShapeType::Box:
            return IntersectBox(normalizedRay, shape, inverse, localOrigin, localDirection,
                                minDistance, maxDistance, outHit);
        case ShapeType::Sphere:
            return IntersectSphere(normalizedRay, shape, inverse, localOrigin, localDirection,
                                   minDistance, maxDistance, outHit);
    }
    return false;
}

} // namespace Concord::Collision
