#include "engine/motion/Easing.h"

namespace Concord {
namespace Motion {

namespace {

float Clamp01(float t) noexcept
{
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

} // namespace

float Ease(Easing curve, float t) noexcept
{
    t = Clamp01(t);
    switch (curve) {
        case Easing::Linear:
            return t;
        case Easing::InQuad:
            return t * t;
        case Easing::OutQuad:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case Easing::InOutQuad:
            return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * 0.5f;
        case Easing::InCubic:
            return t * t * t;
        case Easing::OutCubic: {
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case Easing::InOutCubic:
            if (t < 0.5f) {
                return 4.0f * t * t * t;
            } else {
                const float u = -2.0f * t + 2.0f;
                return 1.0f - u * u * u * 0.5f;
            }
        case Easing::OutBack: {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            const float u = t - 1.0f;
            return 1.0f + c3 * u * u * u + c1 * u * u;
        }
        case Easing::OutBounce: {
            constexpr float n1 = 7.5625f;
            constexpr float d1 = 2.75f;
            if (t < 1.0f / d1) {
                return n1 * t * t;
            } else if (t < 2.0f / d1) {
                const float u = t - 1.5f / d1;
                return n1 * u * u + 0.75f;
            } else if (t < 2.5f / d1) {
                const float u = t - 2.25f / d1;
                return n1 * u * u + 0.9375f;
            } else {
                const float u = t - 2.625f / d1;
                return n1 * u * u + 0.984375f;
            }
        }
    }
    return t;
}

} // namespace Motion
} // namespace Concord
