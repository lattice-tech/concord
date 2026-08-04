#include "engine/app/Game.h"

#include "engine/app/ConfigLocator.h"
#include "engine/app/ConsoleHost.h"
#include "engine/app/GameConfigLoader.h"
#include "engine/debug/Logger.h"
#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/scene/Scene.h"
#include "engine/window/Window.h"

namespace Concord {

Game::Game(const GameConfig& fallbackConfig, const std::string& configPath)
    : m_config(GameConfigLoader::LoadFromFile(ConfigLocator::Resolve(configPath), fallbackConfig))
    , m_loop(EngineLoop::Acquire(m_config.renderBackend))
{
    // Apply the console policy before any subsystem logs: Release runs hide
    // the stub console a console-subsystem exe opens, Debug runs keep the
    // launching terminal or allocate a log console when launched without one.
    ApplyConsolePolicy(m_config.mode);
}

Game::~Game()
{
    Destroy();
}

void Game::AttachWindow(Window& window)
{
    if (!m_loop) {
        return;
    }
    DetachWindow(); // a Game owns at most one window; replace, don't stack.

    // The config's `antialiasing=` key seeds the window's AA if the caller
    // left it at its default; a caller who set a specific AA on the WindowDesc
    // wins. The full AntiAliasing mode (not just the MSAA level) travels with
    // the window's description through the request queue to the render thread.
    if (window.Antialiasing() == AntiAliasing::Fxaa) {
        window.SetAntialiasing(m_config.antialiasing); // not yet attached: local only
    }
    m_loop->SetAntiAliasing(window.Antialiasing());

    m_windowId = m_loop->AttachWindow(window);
    window.BindLive(m_loop, m_windowId);
    m_liveWindow = m_windowId != EngineLoop::kInvalidWindowId ? &window : nullptr;
    if (m_activeScene != nullptr) {
        m_activeScene->RebindWindow(m_windowId);
    }
}

void Game::DetachWindow()
{
    if (m_activeScene != nullptr) {
        m_activeScene->RebindWindow(EngineLoop::kInvalidWindowId);
    }
    if (m_loop && m_windowId != EngineLoop::kInvalidWindowId) {
        m_loop->DetachWindow(m_windowId);
        m_windowId = EngineLoop::kInvalidWindowId;
    }
    if (m_liveWindow != nullptr) {
        m_liveWindow->BindLive({}, EngineLoop::kInvalidWindowId);
        m_liveWindow = nullptr;
    }
}

void Game::LoadScene(Scene& scene)
{
    if (!m_loop) {
        return;
    }
    if (m_activeScene == &scene) {
        return;
    }
    if (!scene.Bind(this, m_loop, m_windowId)) {
        return;
    }

    Scene* previous = m_activeScene;
    m_activeScene = &scene;
    if (previous != nullptr) {
        previous->Unbind();
    }
    scene.ActivateBinding();
}

void Game::UnloadScene()
{
    if (m_activeScene != nullptr) {
        m_activeScene->Unbind();
        m_activeScene = nullptr;
    }
}

void Game::ForgetScene(Scene* scene)
{
    if (m_activeScene == scene) {
        m_activeScene = nullptr;
    }
}

void Game::OnUpdate(std::function<void(float)> onUpdate)
{
    if (!m_loop) {
        return;
    }
    if (m_updateId != EngineLoop::kInvalidUpdateId) {
        m_loop->RemoveUpdate(m_updateId); // at most one callback per Game; replace, don't stack.
        m_updateId = EngineLoop::kInvalidUpdateId;
    }
    if (onUpdate) {
        m_updateId = m_loop->OnUpdate(std::move(onUpdate));
    }
}

UpdateSubscription Game::SubscribeUpdate(std::function<void(float)> onUpdate)
{
    if (!m_loop || !onUpdate) {
        return {};
    }
    std::erase_if(m_subscriptions, [](const std::weak_ptr<UpdateSubscription::State>& subscription) {
        return subscription.expired();
    });
    const EngineLoop::UpdateId id = m_loop->OnUpdate(std::move(onUpdate));
    if (id == EngineLoop::kInvalidUpdateId) {
        return {};
    }
    auto state = std::make_shared<UpdateSubscription::State>(m_loop, id);
    m_subscriptions.push_back(state);
    return UpdateSubscription(std::move(state));
}

float Game::DeltaTime() const noexcept
{
    return m_loop ? m_loop->DeltaTime() : 0.0f;
}

std::uint64_t Game::FrameCount() const noexcept
{
    return m_loop ? m_loop->FrameCount() : 0;
}

float Game::Fps() const noexcept
{
    return m_loop ? m_loop->Fps() : 0.0f;
}

FrameStats Game::Stats() const noexcept
{
    return m_loop ? m_loop->Stats() : FrameStats{};
}

void Game::Run()
{
    if (m_loop && m_windowId != EngineLoop::kInvalidWindowId) {
        m_loop->WaitForWindowClose(m_windowId);
    }
}

void Game::Destroy()
{
    if (m_destroyed) {
        return;
    }

    // Establish callback barriers before tearing down any state callbacks may
    // still reach. Self-removal is non-blocking when Destroy runs inside the
    // callback itself; off-thread removal waits for in-flight execution.
    OnUpdate(nullptr);
    for (const std::weak_ptr<UpdateSubscription::State>& weakSubscription : m_subscriptions) {
        if (const std::shared_ptr<UpdateSubscription::State> subscription = weakSubscription.lock()) {
            subscription->Reset();
        }
    }
    m_subscriptions.clear();

    UnloadScene();
    DetachWindow();
    m_loop.reset(); // last Game to let go stops the shared render thread.

    m_destroyed = true;
}

} // namespace Concord
