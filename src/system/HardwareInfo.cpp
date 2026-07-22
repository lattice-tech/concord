#include "system/HardwareInfo.h"

#include <SDL3/SDL.h>

namespace Concord {

HardwareInfo QueryHardwareInfo()
{
    HardwareInfo info;

    // SDL_GetPlatform returns a static string owned by SDL; copy it into our
    // std::string so the value stays valid independent of SDL's lifetime.
    if (const char* platform = SDL_GetPlatform()) {
        info.platform = platform;
    }

    info.logicalCores = SDL_GetNumLogicalCPUCores();
    info.systemRamMB = SDL_GetSystemRAM();
    info.cpuCacheLineSize = SDL_GetCPUCacheLineSize();

    info.hasSSE42 = SDL_HasSSE42();
    info.hasAVX = SDL_HasAVX();
    info.hasAVX2 = SDL_HasAVX2();
    info.hasNEON = SDL_HasNEON();

    return info;
}

} // namespace Concord
