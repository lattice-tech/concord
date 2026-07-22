#include "engine/utils/PlatformUtils.h"

#include "engine/window/Window.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace Concord {

bool OpenUrl(const std::string& url)
{
    if (url.empty()) {
        return false;
    }

    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           url.data(), static_cast<int>(url.size()),
                                           nullptr, 0);
    if (length <= 0) {
        return false;
    }

    std::wstring wideUrl(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            url.data(), static_cast<int>(url.size()),
                            wideUrl.data(), length) != length) {
        return false;
    }

    const HINSTANCE result = ShellExecuteW(nullptr, L"open", wideUrl.c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

bool OpenUrl(const char* url)
{
    return url != nullptr && OpenUrl(std::string(url));
}

void ShowWindow(Window& window)
{
    window.SetVisible(true);
}

void HideWindow(Window& window)
{
    window.SetVisible(false);
}

void ShowMouse(Window& window)
{
    window.SetShowCursor(true);
}

void HideMouse(Window& window)
{
    window.SetShowCursor(false);
}

} // namespace Concord
