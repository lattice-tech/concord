#ifndef CONCORD_SKELETALBLEND_H
#define CONCORD_SKELETALBLEND_H

#include "engine/animation/AnimationTrack.h"
#include "engine/animation/SkeletalClip.h"
#include "engine/animation/Skeleton.h"

#include <algorithm>
#include <vector>

namespace Concord::Animation {

/**
 * Blends two skeleton poses bone-by-bone by weight @p t (0 -> a, 1 -> b):
 * position/scale lerp, rotation slerp. `out` is sized to `a`; bones present
 * only in `a` keep a's value. This is the per-skeleton analogue of BlendPose,
 * used by skeletal crossfades and blend spaces.
 */
inline void BlendSkeletonPose(const SkeletonPose& a, const SkeletonPose& b, float t, SkeletonPose& out)
{
    out.local.resize(a.local.size());
    const std::size_t shared = std::min(a.local.size(), b.local.size());
    for (std::size_t i = 0; i < shared; ++i) {
        out.local[i].position = AnimInterpolate(a.local[i].position, b.local[i].position, t);
        out.local[i].rotation = AnimInterpolate(a.local[i].rotation, b.local[i].rotation, t);
        out.local[i].scale = AnimInterpolate(a.local[i].scale, b.local[i].scale, t);
    }
    for (std::size_t i = shared; i < a.local.size(); ++i) {
        out.local[i] = a.local[i];
    }
}

/**
 * A one-dimensional blend space over skeletal clips: several clips placed along
 * a scalar axis, the whole-skeleton pose interpolated between the two clips
 * bracketing a control value. The skeletal analogue of BlendSpace1D — a
 * locomotion state uses it to blend idle/walk/run by a `speed` parameter.
 *
 * Clips are phase-synchronised (all sampled at the same normalised phase) so
 * footfalls line up. A null clip entry samples the skeleton's bind pose, which
 * is a convenient "idle" anchor when a model ships only locomotion clips.
 * Clips are referenced, not owned.
 */
class SkeletalBlendSpace1D {
public:
    void AddClip(float axis, const SkeletalClip* clip)
    {
        Entry entry{axis, clip};
        auto it = m_entries.begin();
        while (it != m_entries.end() && it->axis <= axis) {
            ++it;
        }
        m_entries.insert(it, entry);
    }

    bool Empty() const noexcept { return m_entries.empty(); }

    /** Longest referenced clip duration (drives the shared phase clock). */
    float Duration() const noexcept
    {
        float d = 0.0f;
        for (const Entry& e : m_entries) {
            if (e.clip != nullptr) {
                d = std::max(d, e.clip->Duration());
            }
        }
        return d;
    }

    /**
     * Samples the pose at control value @p value and normalised phase @p phase
     * over @p skeleton into @p out. Clamps to the end clips; blends the two
     * bracketing clips by their axis positions.
     */
    void Sample(float value, float phase, const Skeleton& skeleton, SkeletonPose& out) const
    {
        if (m_entries.empty()) {
            out = skeleton.BindPose();
            return;
        }
        if (m_entries.size() == 1 || value <= m_entries.front().axis) {
            SampleEntry(m_entries.front(), phase, skeleton, out);
            return;
        }
        if (value >= m_entries.back().axis) {
            SampleEntry(m_entries.back(), phase, skeleton, out);
            return;
        }
        std::size_t i = 0;
        while (i + 1 < m_entries.size() && m_entries[i + 1].axis <= value) {
            ++i;
        }
        const Entry& a = m_entries[i];
        const Entry& b = m_entries[i + 1];
        const float span = b.axis - a.axis;
        const float t = span > 1e-6f ? (value - a.axis) / span : 0.0f;
        SkeletonPose poseA;
        SkeletonPose poseB;
        SampleEntry(a, phase, skeleton, poseA);
        SampleEntry(b, phase, skeleton, poseB);
        BlendSkeletonPose(poseA, poseB, t, out);
    }

private:
    struct Entry {
        float axis = 0.0f;
        const SkeletalClip* clip = nullptr;
    };

    static void SampleEntry(const Entry& e, float phase, const Skeleton& skeleton, SkeletonPose& out)
    {
        if (e.clip == nullptr) {
            out = skeleton.BindPose();
            return;
        }
        const float clamped = phase < 0.0f ? 0.0f : (phase > 1.0f ? 1.0f : phase);
        e.clip->Sample(clamped * e.clip->Duration(), skeleton, out);
    }

    std::vector<Entry> m_entries;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALBLEND_H
