#ifndef CONCORD_BLENDSPACE1D_H
#define CONCORD_BLENDSPACE1D_H

#include "engine/animation/clip/AnimationClip.h"
#include "engine/animation/clip/SyncTrack.h"
#include "engine/animation/blend/Pose.h"

#include <algorithm>
#include <cstddef>
#include <string>
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
        return SampleImpl(value, phase, nullptr);
    }

    /**
     * @brief Like Sample, but aligns the blended clips through @p syncName.
     *
     * The first clip drives a master timeline; every other clip's sample time
     * maps onto it through the shared marker (SyncTrack::MapTime), so loops of
     * different lengths stay lined up at the marker. Falls back to plain phase
     * sampling when the marker is missing on either side.
     */
    Pose SampleSynced(float value, float phase, const std::string& syncName) const
    {
        return SampleImpl(value, phase, &syncName);
    }

private:
    struct Entry {
        float axis = 0.0f;
        const AnimationClip* clip = nullptr;
    };

    static Pose SampleEntryAt(const Entry& e, float time)
    {
        if (e.clip == nullptr) {
            return Pose{};
        }
        return e.clip->SamplePose(time);
    }

    /** Phase-scaled sample time for an entry (0 for a null clip). */
    static float PhaseTime(const Entry& e, float clampedPhase)
    {
        return e.clip != nullptr ? clampedPhase * e.clip->Duration() : 0.0f;
    }

    Pose SampleImpl(float value, float phase, const std::string* syncName) const
    {
        if (m_entries.empty()) {
            return Pose{};
        }
        const float clamped = phase < 0.0f ? 0.0f : (phase > 1.0f ? 1.0f : phase);

        if (syncName == nullptr) {
            if (m_entries.size() == 1 || value <= m_entries.front().axis) {
                return SampleEntryAt(m_entries.front(), PhaseTime(m_entries.front(), clamped));
            }
            if (value >= m_entries.back().axis) {
                return SampleEntryAt(m_entries.back(), PhaseTime(m_entries.back(), clamped));
            }
            std::size_t i = 0;
            while (i + 1 < m_entries.size() && m_entries[i + 1].axis <= value) {
                ++i;
            }
            const Entry& a = m_entries[i];
            const Entry& b = m_entries[i + 1];
            const float span = b.axis - a.axis;
            const float t = span > 1e-6f ? (value - a.axis) / span : 0.0f;
            return BlendPose(SampleEntryAt(a, PhaseTime(a, clamped)),
                             SampleEntryAt(b, PhaseTime(b, clamped)), t);
        }

        // Synced path: the first entry drives a master timeline; every other
        // clip's sample time maps onto it through the shared marker.
        std::vector<float> times(m_entries.size(), 0.0f);
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            const AnimationClip* clip = m_entries[i].clip;
            if (clip == nullptr) {
                continue;
            }
            const AnimationClip* master = m_entries.front().clip;
            if (i == 0 || master == nullptr) {
                times[i] = clamped * clip->Duration();
                continue;
            }
            times[i] = SyncTrack::MapTime(clamped * master->Duration(),
                                          master->sync, clip->sync, *syncName,
                                          master->Duration(), clip->Duration());
        }

        if (m_entries.size() == 1 || value <= m_entries.front().axis) {
            return SampleEntryAt(m_entries.front(), times.front());
        }
        if (value >= m_entries.back().axis) {
            return SampleEntryAt(m_entries.back(), times.back());
        }
        std::size_t i = 0;
        while (i + 1 < m_entries.size() && m_entries[i + 1].axis <= value) {
            ++i;
        }
        const Entry& a = m_entries[i];
        const Entry& b = m_entries[i + 1];
        const float span = b.axis - a.axis;
        const float t = span > 1e-6f ? (value - a.axis) / span : 0.0f;
        return BlendPose(SampleEntryAt(a, times[i]), SampleEntryAt(b, times[i + 1]), t);
    }

    std::vector<Entry> m_entries;
};

} // namespace Concord::Animation

#endif // CONCORD_BLENDSPACE1D_H
