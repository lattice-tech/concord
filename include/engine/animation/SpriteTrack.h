#ifndef CONCORD_SPRITETRACK_H
#define CONCORD_SPRITETRACK_H

#include <cstdint>
#include <vector>

namespace Concord::Animation {

/** One frame of a 2D sprite animation: which atlas cell, held for `duration` s. */
struct SpriteFrame {
    int index = 0;
    float duration = 0.1f;
};

/**
 * A 2D frame-sequence channel: an ordered list of atlas cell indices, each
 * shown for its own duration. Unlike the TRS tracks this is *stepped*, not
 * interpolated — sprite frames snap, they do not blend — so Sample returns the
 * integer index active at a given time.
 *
 * The engine has no sprite renderer yet, so this produces the frame *index*
 * and leaves it to the caller to map that onto a texture / atlas cell. That
 * keeps the 2D animation path usable now (state machines can drive a frame
 * index) and ready to wire into a sprite draw later.
 */
class SpriteTrack {
public:
    void AddFrame(int index, float duration)
    {
        m_frames.push_back({index, duration > 0.0f ? duration : 0.001f});
        m_total += m_frames.back().duration;
    }

    bool Empty() const noexcept { return m_frames.empty(); }

    /** Total run time of one pass through all frames. */
    float Duration() const noexcept { return m_total; }

    /**
     * Frame index active at @p time (clamped to the sequence). Returns the
     * first frame's index before the start and the last frame's after the end;
     * -1 only when the track is empty.
     */
    int Sample(float time) const noexcept
    {
        if (m_frames.empty()) {
            return -1;
        }
        if (time <= 0.0f) {
            return m_frames.front().index;
        }
        float acc = 0.0f;
        for (const SpriteFrame& f : m_frames) {
            acc += f.duration;
            if (time < acc) {
                return f.index;
            }
        }
        return m_frames.back().index;
    }

private:
    std::vector<SpriteFrame> m_frames;
    float m_total = 0.0f;
};

} // namespace Concord::Animation

#endif // CONCORD_SPRITETRACK_H
