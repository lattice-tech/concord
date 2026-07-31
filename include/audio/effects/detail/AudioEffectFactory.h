#ifndef CONCORD_AUDIOEFFECTFACTORY_H
#define CONCORD_AUDIOEFFECTFACTORY_H

#include "audio/effects/AudioEffectDesc.h"
#include "audio/effects/IAudioEffect.h"

#include <memory>

namespace Concord::Audio::Detail {

/**
 * Instantiates the concrete effect matching `desc.type`, initialised for
 * `sampleRate`. Returns nullptr when the type is unknown or Init fails.
 */
std::unique_ptr<IAudioEffect> CreateEffect(const AudioEffectDesc& desc,
                                           std::uint32_t sampleRate);

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOEFFECTFACTORY_H
