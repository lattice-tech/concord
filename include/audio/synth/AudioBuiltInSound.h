#ifndef CONCORD_AUDIOBUILTINSOUND_H
#define CONCORD_AUDIOBUILTINSOUND_H

#include <cstdint>

namespace Concord::Audio {

/** Built-in synthesized sound presets shipped by CAudio.dll. */
enum class AudioBuiltInSound : std::uint8_t {
    Beep = 0,
    Click,
    Confirm,
    Error,
    Powerup,
    Laser,
    Explosion,
    EngineHum,
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOBUILTINSOUND_H
