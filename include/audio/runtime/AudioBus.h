#ifndef CONCORD_AUDIOBUS_H
#define CONCORD_AUDIOBUS_H

#include <cstdint>

namespace Concord::Audio {

/** Fixed runtime bus set owned by CAudio.dll. */
enum class AudioBusId : std::uint8_t {
    Master = 0,
    Music,
    Sfx,
    Ui,
    Dialogue,
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOBUS_H
