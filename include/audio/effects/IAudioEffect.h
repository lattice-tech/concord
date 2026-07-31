#ifndef CONCORD_IAUDIOEFFECT_H
#define CONCORD_IAUDIOEFFECT_H

#include <cstdint>

namespace Concord::Audio {

/**
 * @brief Insert effect processing interleaved stereo audio in place.
 *
 * Effects are owned by an AudioBus and run in chain order on the mixer
 * thread; implementations must be allocation-free inside Process.
 */
class IAudioEffect {
public:
    virtual ~IAudioEffect() = default;

    /** Prepares internal state for the given sample rate. */
    virtual bool Init(std::uint32_t sampleRate) = 0;

    /** Releases resources; the effect may be re-initialised afterwards. */
    virtual void Shutdown() noexcept = 0;

    /** Processes `frames` interleaved stereo frames in place. */
    virtual void Process(float* interleavedStereo, std::uint32_t frames) = 0;

    /** Resets tails/history (e.g. on bus rewire) without reallocation. */
    virtual void Reset() noexcept = 0;
};

} // namespace Concord::Audio

#endif // CONCORD_IAUDIOEFFECT_H
