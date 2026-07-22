#ifndef CONCORD_BLENDSPACE1D_H
#define CONCORD_BLENDSPACE1D_H

#include "engine/animation/AnimationClip.h"
#include "engine/animation/Pose.h"

#include <algorithm>
#include <vector>

namespace Concord::Animation {

/**
 * A one-dimensional blend space: several clips placed along a scalar axis, the
 * pose smoothly interpolated between the two clips bracketing a control value.
 *
 * This is the "blend tree" a locomotion state uses — e.g. idle at 0, walk at
 * 3, run at 6 along a `speed` axis; feeding speed = 4.5 blends walk and run
 * 50/50. Clips are phase-synchronised: all sampled at the same normalised
 * phase [0,1) so their footfalls line up regardless of individual lengths, the
 * standard fix for foot-sliding when blending locomotion of different speeds.
 *
 * The clips are referenced, not owned, so they must outlive the blend space.
 */
class BlendSpace1D {
public:
    /** Adds a clip at position @p axis on the blend axis (order-independent). */
    void AddClip(float axis, const AnimationClip* clip)
    {
        Entry entry{axis, clip};
        auto it = m_entries.begin();
        while (it != m_entries.end() && it->axis <= axis) {
            ++it;
        }
        m_entries.insert(it, entry);
    }

    bool Empty() const noexcept { return m_entries.empty(); }

    /**
     * The longest referenced clip's duration — the blend space's own duration,
     * so a driver can advance a shared phase clock over it.
     */
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
     * Pose at control value @p value and normalised phase @p phase in [0,1).
     * Clamps @p value to the end clips; blends the two bracketing clips by
     * their axis positions. Each clip is sampled at `phase * clipDuration`.
     */
    Pose Sample(float value, float phase) const
    {
        if (m_entries.empty()) {
            return Pose{};
        }
        if (m_entries.size() == 1 || value <= m_entries.front().axis) {
            return SampleEntry(m_entries.front(), phase);
        }
        if (value >= m_entries.back().axis) {
            return SampleEntry(m_entries.back(), phase);
        }
        std::size_t i = 0;
        while (i + 1 < m_entries.size() && m_entries[i + 1].axis <= value) {
            ++i;
        }
        const Entry& a = m_entries[i];
        const Entry& b = m_entries[i + 1];
        const float span = b.axis - a.axis;
        const float t = span > 1e-6f ? (value - a.axis) / span : 0.0f;
        return BlendPose(SampleEntry(a, phase), SampleEntry(b, phase), t);
    }

private:
    struct Entry {
        float axis = 0.0f;
        const AnimationClip* clip = nullptr;
    };

    static Pose SampleEntry(const Entry& e, float phase)
    {
        if (e.clip == nullptr) {
            return Pose{};
        }
        const float clamped = phase < 0.0f ? 0.0f : (phase > 1.0f ? 1.0f : phase);
        return e.clip->SamplePose(clamped * e.clip->Duration());
    }

    std::vector<Entry> m_entries;
};

} // namespace Concord::Animation

#endif // CONCORD_BLENDSPACE1D_H
