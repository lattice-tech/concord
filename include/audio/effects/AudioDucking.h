#ifndef CONCORD_AUDIODUCKING_H
#define CONCORD_AUDIODUCKING_H

#include "audio/runtime/AudioBus.h"

namespace Concord::Audio {

/**
 * @brief Sidechain ducking rule: while `trigger` carries signal above
 * `thresholdLinear`, the `target` bus is attenuated toward `duckedGain`.
 *
 * The gain moves with the attack time constant while triggered and recovers
 * with the release time constant, so dialogue can push music down smoothly
 * and let it swell back after the line ends.
 */
struct AudioDuckingDesc {
    AudioBusId trigger = AudioBusId::Dialogue;
    AudioBusId target = AudioBusId::Music;
    float thresholdLinear = 0.02f;
    float duckedGain = 0.35f;
    float attackSeconds = 0.040f;
    float releaseSeconds = 0.400f;
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIODUCKING_H
