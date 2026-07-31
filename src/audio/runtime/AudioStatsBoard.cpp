#include "audio/runtime/detail/AudioStatsBoard.h"

namespace Concord::Audio::Detail {

void AudioStatsBoard::Reset() noexcept
{
    initialized.store(false, std::memory_order_relaxed);
    activeVoices.store(0, std::memory_order_relaxed);
    activeSpatialVoices.store(0, std::memory_order_relaxed);
    queuedCommands.store(0, std::memory_order_relaxed);
    rejectedCommands.store(0, std::memory_order_relaxed);
    underrunCount.store(0, std::memory_order_relaxed);
    recoverCount.store(0, std::memory_order_relaxed);
    callbackCpuMs.store(0.0f, std::memory_order_relaxed);
    peakMaster.store(0.0f, std::memory_order_relaxed);
    peakMusic.store(0.0f, std::memory_order_relaxed);
    peakSfx.store(0.0f, std::memory_order_relaxed);
    peakUi.store(0.0f, std::memory_order_relaxed);
    peakDialogue.store(0.0f, std::memory_order_relaxed);
    peakAmbience.store(0.0f, std::memory_order_relaxed);
}

AudioStats AudioStatsBoard::Snapshot() const noexcept
{
    AudioStats out;
    out.initialized = initialized.load(std::memory_order_relaxed);
    out.activeVoices = activeVoices.load(std::memory_order_relaxed);
    out.activeSpatialVoices = activeSpatialVoices.load(std::memory_order_relaxed);
    out.queuedCommands = queuedCommands.load(std::memory_order_relaxed);
    out.rejectedCommands = rejectedCommands.load(std::memory_order_relaxed);
    out.underrunCount = underrunCount.load(std::memory_order_relaxed);
    out.recoverCount = recoverCount.load(std::memory_order_relaxed);
    out.callbackCpuMs = callbackCpuMs.load(std::memory_order_relaxed);
    out.peakMaster = peakMaster.load(std::memory_order_relaxed);
    out.peakMusic = peakMusic.load(std::memory_order_relaxed);
    out.peakSfx = peakSfx.load(std::memory_order_relaxed);
    out.peakUi = peakUi.load(std::memory_order_relaxed);
    out.peakDialogue = peakDialogue.load(std::memory_order_relaxed);
    out.peakAmbience = peakAmbience.load(std::memory_order_relaxed);
    return out;
}

} // namespace Concord::Audio::Detail
