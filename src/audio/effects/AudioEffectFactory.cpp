#include "audio/effects/detail/AudioEffectFactory.h"

#include "audio/effects/detail/CompressorEffect.h"
#include "audio/effects/detail/FilterEffect.h"
#include "audio/effects/detail/LimiterEffect.h"
#include "audio/effects/detail/ReverbEffect.h"

namespace Concord::Audio::Detail {

std::unique_ptr<IAudioEffect> CreateEffect(const AudioEffectDesc& desc,
                                           std::uint32_t sampleRate)
{
    std::unique_ptr<IAudioEffect> effect;
    switch (desc.type) {
    case AudioEffectType::LowPass:
        effect = std::make_unique<LowPassEffect>(desc.filter);
        break;
    case AudioEffectType::HighPass:
        effect = std::make_unique<HighPassEffect>(desc.filter);
        break;
    case AudioEffectType::Compressor:
        effect = std::make_unique<CompressorEffect>(desc.compressor);
        break;
    case AudioEffectType::Reverb:
        effect = std::make_unique<ReverbEffect>(desc.reverb);
        break;
    case AudioEffectType::Limiter:
        effect = std::make_unique<LimiterEffect>(desc.limiter);
        break;
    }
    if (!effect || !effect->Init(sampleRate)) {
        return nullptr;
    }
    return effect;
}

} // namespace Concord::Audio::Detail
