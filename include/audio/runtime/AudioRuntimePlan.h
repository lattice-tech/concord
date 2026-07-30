#ifndef CONCORD_AUDIORUNTIMEPLAN_H
#define CONCORD_AUDIORUNTIMEPLAN_H

namespace Concord::Audio {

/**
 * @brief Placeholder marker for the planned runtime audio layer.
 *
 * The current `CAudio` module only ships the Steam Audio HRTF DSP core
 * (`SteamAudioSpatializer`). The full runtime layer (device, mixer, voices,
 * buses, streaming) is intentionally specified first in
 * `docs/音频系统规划.md` so future implementation can grow behind a stable,
 * explicit architecture instead of accreting ad-hoc APIs.
 */
struct AudioRuntimePlan final {};

} // namespace Concord::Audio

#endif // CONCORD_AUDIORUNTIMEPLAN_H
