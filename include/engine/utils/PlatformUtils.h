#ifndef CONCORD_PLATFORMUTILS_H
#define CONCORD_PLATFORMUTILS_H

#include "Concord/CExport.h"

#include <string>

namespace Concord {

class Window;

/**
 * Opens a URL with the operating system's registered handler.
 *
 * This function does not access SDL or render-thread state and is safe to
 * call from engine callbacks.
 *
 * @param url UTF-8 URL to open.
 * @return True when Windows accepted the open request.
 */
CENGINE_API bool OpenUrl(const std::string& url);

/**
 * Opens a URL with the operating system's registered handler.
 * @param url Null-terminated UTF-8 URL; null and empty strings are rejected.
 * @return True when Windows accepted the open request.
 */
CENGINE_API bool OpenUrl(const char* url);

/** @brief Shows a window, applying the change on the render thread if attached. */
CENGINE_API void ShowWindow(Window& window);

/** @brief Hides a window, applying the change on the render thread if attached. */
CENGINE_API void HideWindow(Window& window);

/** @brief Makes the process-wide OS mouse cursor visible when not captured. */
CENGINE_API void ShowMouse(Window& window);

/** @brief Hides the process-wide OS mouse cursor when not captured. */
CENGINE_API void HideMouse(Window& window);

} // namespace Concord

#endif // CONCORD_PLATFORMUTILS_H
