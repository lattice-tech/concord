#include "engine/app/ConsoleHost.h"

#ifdef _WIN32
// Must precede the C++ standard headers: MinGW's winbase.h defines CRT
// aliases (freopen, abs, ...) that corrupt the global namespace otherwise.
// intrin.h first resolves winbase.h's Interlocked* inline wrappers against
// the compiler builtins instead of mis-matched MinGW declarations.
#include <intrin.h>
#include <windows.h>
#endif

#include <cstdio>
#include <mutex>

namespace Concord {
namespace {

std::once_flag g_policyOnce;

#ifdef _WIN32
void AttachDebugConsole()
{
    if (GetConsoleWindow() != nullptr) {
        return; // launched from a terminal: keep that console
    }
    if (AllocConsole() == 0) {
        return;
    }
    // Route the CRT streams to the fresh console so the logger and any
    // printf-style output land where the developer can see them.
    (void)std::freopen("CONOUT$", "w", stdout);
    (void)std::freopen("CONOUT$", "w", stderr);
    (void)std::freopen("CONIN$", "r", stdin);
}

void HideStubConsole()
{
    // A console-subsystem exe double-clicked opens a stub console; Release
    // runs never need it, so detach and leave the game window as the only UI.
    if (GetConsoleWindow() != nullptr) {
        FreeConsole();
    }
}
#endif

} // namespace

void ApplyConsolePolicy(RuntimeMode mode) noexcept
{
    std::call_once(g_policyOnce, [mode]() {
#ifdef _WIN32
        if (mode == RuntimeMode::Release) {
            HideStubConsole();
        } else {
            AttachDebugConsole();
        }
#else
        (void)mode;
#endif
    });
}

} // namespace Concord
