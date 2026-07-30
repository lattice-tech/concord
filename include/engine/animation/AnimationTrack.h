#ifndef CONCORD_ANIMATIONTRACK_H
#define CONCORD_ANIMATIONTRACK_H

#include "engine/motion/Easing.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

#include <cmath>
#include <vector>

namespace Concord::Animation {

/**
 * One keyed value on a timeline: the value the animated channel should hold
 * at @p time seconds. `ease` is the interpolation curve used across the
 * segment that *ends* at this key, so authors can, e.g., ease-out into a
 * settle pose by tagging the destination key.
 */
template <typename T>
struct Keyframe {
    float time = 0.0f;
    T value{};
    Motion::Easing ease = Motion::Easing::Linear;
};

/** Linear blend of two scalars. */
inline float AnimInterpolate(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** Component-wise linear blend of two vectors (positions, scales). */
inline Vector3 AnimInterpolate(const Vector3& a, const Vector3& b, float t) noexcept
{
    return Vector3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

/**
 * Shortest-path spherical linear blend of two rotations. Falls back to a
 * normalised linear blend for nearly-parallel quaternions (where sin(theta)
 * underflows), which stays numerically stable and visually identical there.
 */
inline Quaternion AnimInterpolate(const Quaternion& a, Quaternion b, float t) noexcept
{
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    // Take the shorter arc: negate one quaternion if they are more than 90
    // degrees apart, so the interpolation never spins the long way round.
    if (dot < 0.0f) {
        b = Quaternion{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }

    Quaternion result;
    if (dot > 0.9995f) {
        // Nearly parallel: linear blend then normalise.
        result = Quaternion{
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        };
    } else {
        const float theta0 = std::acos(dot);
        const float theta = theta0 * t;
        const float sinTheta0 = std::sin(theta0);
        const float s0 = std::sin(theta0 - theta) / sinTheta0;
        const float s1 = std::sin(theta) / sinTheta0;
        result = Quaternion{
            a.x * s0 + b.x * s1,
            a.y * s0 + b.y * s1,
            a.z * s0 + b.z * s1,
            a.w * s0 + b.w * s1,
        };
    }

    const float len = std::sqrt(result.x * result.x + result.y * result.y
                              + result.z * result.z + result.w * result.w);
    if (len > 1e-8f) {
        const float inv = 1.0f / len;
        result.x *= inv; result.y *= inv; result.z *= inv; result.w *= inv;
    }
    return result;
}

/**
 * A single animated channel: a time-sorted list of keyframes of one value
 * type (Vector3 for position/scale, Quaternion for rotation, float for a
 * scalar parameter). Sampling clamps to the ends and eases each segment by
 * its destination key's curve.
 *
 * Header-only template: the engine instantiates it for the TRS types, and a
 * game can instantiate it for its own scalar channels. Keys are kept sorted
 * on insertion so Sample is a simple ordered walk.
 */
template <typename T>
class AnimationTrack {
public:
    /** Adds a keyframe, keeping the track sorted by time. */
    void AddKey(float time, const T& value, Motion::Easing ease = Motion::Easing::Linear)
    {
        Keyframe<T> key{time, value, ease};
        auto it = m_keys.begin();
        while (it != m_keys.end() && it->time <= time) {
            ++it;
        }
        m_keys.insert(it, key);
    }

    bool Empty() const noexcept { return m_keys.empty(); }

    /** Time of the last keyframe (0 when empty). */
    float Duration() const noexcept { return m_keys.empty() ? 0.0f : m_keys.back().time; }

    /** The time-sorted keyframes, for serialization and inspection. */
    const std::vector<Keyframe<T>>& Keys() const noexcept { return m_keys; }

    /**
     * Value at @p time. Before the first key returns the first value; after
     * the last, the last value; between keys, the eased blend of the bracketing
     * pair. Returns a default-constructed T when the track has no keys.
     */
    T Sample(float time) const
    {
        if (m_keys.empty()) {
            return T{};
        }
        if (time <= m_keys.front().time) {
            return m_keys.front().value;
        }
        if (time >= m_keys.back().time) {
            return m_keys.back().value;
        }
        std::size_t i = 0;
        while (i + 1 < m_keys.size() && m_keys[i + 1].time <= time) {
            ++i;
        }
        const Keyframe<T>& k0 = m_keys[i];
        const Keyframe<T>& k1 = m_keys[i + 1];
        const float span = k1.time - k0.time;
        const float localT = span > 1e-8f ? (time - k0.time) / span : 0.0f;
        const float eased = Motion::Ease(k1.ease, localT);
        return AnimInterpolate(k0.value, k1.value, eased);
    }

private:
    std::vector<Keyframe<T>> m_keys;
};

} // namespace Concord::Animation

#endif // CONCORD_ANIMATIONTRACK_H
