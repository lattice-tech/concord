#ifndef CONCORD_PLAYBACKMODE_H
#define CONCORD_PLAYBACKMODE_H

#include <cstdint>

namespace Concord::Animation {

/**
 * How an AnimationPlayer treats the end of a clip.
 *
 * The three modes cover the common cases a state machine's states need: a
 * one-shot action (Once), an idle/run cycle (Loop), and a back-and-forth
 * breathing/hover motion (PingPong).
 */
enum class PlaybackMode : std::uint8_t {
    /** Play once to the end, then hold the final pose and report finished. */
    Once = 0,

    /** Restart from the beginning each time the end is reached. */
    Loop = 1,

    /** Reverse direction at each end, oscillating between the two extremes. */
    PingPong = 2,
};

} // namespace Concord::Animation

#endif // CONCORD_PLAYBACKMODE_H
