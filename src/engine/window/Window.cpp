#include "engine/window/Window.h"

#include <utility>

namespace Concord {

Window::Window(WindowDesc desc)
    : m_desc(std::move(desc))
{
}

const WindowDesc& Window::Desc() const noexcept
{
    return m_desc;
}

const std::string& Window::Title() const noexcept
{
    return m_desc.title;
}

int Window::Width() const noexcept
{
    return m_desc.resolution.width;
}

int Window::Height() const noexcept
{
    return m_desc.resolution.height;
}

WindowMode Window::Mode() const noexcept
{
    return m_desc.mode;
}

bool Window::Resizable() const noexcept
{
    return m_desc.resizable;
}

bool Window::Visible() const noexcept
{
    return m_desc.visible;
}

bool Window::Vsync() const noexcept
{
    return m_desc.vsync;
}

bool Window::ShowCursor() const noexcept
{
    return m_desc.showCursor;
}

bool Window::CaptureMouse() const noexcept
{
    return m_desc.captureMouse;
}

bool Window::IsMouseCaptured() const noexcept
{
    if (const std::shared_ptr<EngineLoop> loop = m_loop.lock()) {
        return loop->IsMouseCaptured(m_windowId);
    }
    return false;
}

bool Window::ClickToRecapture() const noexcept
{
    return m_desc.clickToRecapture;
}

AntiAliasing Window::Antialiasing() const noexcept
{
    return m_desc.antialiasing;
}

void Window::Set(WindowDesc desc)
{
    m_desc = std::move(desc);
    if (const std::shared_ptr<EngineLoop> loop = m_loop.lock()) {
        loop->UpdateWindow(m_windowId, m_desc);
    }
}

void Window::SetAntialiasing(AntiAliasing aa)
{
    WindowDesc desc = m_desc;
    desc.antialiasing = aa;
    Set(desc);
}

void Window::SetVisible(bool visible)
{
    WindowDesc desc = m_desc;
    desc.visible = visible;
    Set(std::move(desc));
}

void Window::SetShowCursor(bool showCursor)
{
    WindowDesc desc = m_desc;
    desc.showCursor = showCursor;
    Set(std::move(desc));
}

void Window::SetCaptureMouse(bool captureMouse)
{
    WindowDesc desc = m_desc;
    desc.captureMouse = captureMouse;
    Set(std::move(desc));
}

void Window::SetClickToRecapture(bool clickToRecapture)
{
    WindowDesc desc = m_desc;
    desc.clickToRecapture = clickToRecapture;
    Set(std::move(desc));
}

void Window::BindLive(std::weak_ptr<EngineLoop> loop, EngineLoop::WindowId id)
{
    m_loop = std::move(loop);
    m_windowId = id;
}

} // namespace Concord
