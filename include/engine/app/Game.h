#ifndef CONCORD_GAME_H
#define CONCORD_GAME_H

#include "Concord/CExport.h"
#include "engine/app/GameConfig.h"
#include "engine/app/UpdateSubscription.h"
#include "engine/loop/EngineLoop.h"
#include "engine/loop/FrameStats.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Concord {

class Window;
class Scene;

/**
 * Entry point for the engine's lifecycle (CEngine.dll).
 *
 * Each Game owns at most one window, but every Game in the process shares
 * the same EngineLoop: a single render thread, and with it a single bgfx
 * device, back every window the engine opens (see EngineLoop). This lets
 * multiple Game objects coexist cheaply, without duplicating a graphics
 * device per instance. Destroy() closes this Game's window and lets go of
 * the shared loop; the destructor calls it automatically when the caller
 * does not.
 */
class CENGINE_API Game {
public:
    /**
     * @param fallbackConfig Values used for any config key not present in the file.
     * @param configPath Explicit config file path. When empty (the default),
     *        the path is resolved by ConfigLocator: an override set via
     *        ConfigLocator::SetOverridePath if any, otherwise the first
     *        default location that exists (working dir, then executable dir).
     */
    explicit Game(const GameConfig& fallbackConfig = GameConfig{}, const std::string& configPath = "");
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    /**
     * Hands a window to the engine's shared render thread, which opens and
     * drives it. The window's lifetime follows the Game: it is destroyed
     * automatically when the Game is, so it need not be closed by hand. Use
     * DetachWindow to close it earlier. A Game owns at most one window, so
     * calling this again replaces whichever window it currently owns.
     *
     * Takes `window` by non-const reference because attaching also binds
     * it live: afterwards, calling window.Set(...) pushes further changes
     * straight to the open OS window (see docs/窗口.md).
     */
    void AttachWindow(Window& window);

    /** Closes the window currently attached, if any, ahead of Destroy(). */
    void DetachWindow();

    /**
     * Makes `scene` this Game's active scene: its nodes start rendering into
     * the attached window and ticking (OnStart then OnUpdate). Objects are
     * never added to a Game directly — they live in a Scene (see Scene). A
     * Game holds at most one active scene, so calling this again deactivates
     * the previous one and activates `scene`. Attach a window first for the
     * scene's nodes to be visible. `scene` must outlive this call's effect
     * (until another scene is loaded, or the Game or scene is destroyed —
     * whichever comes first is handled safely).
     */
    void LoadScene(Scene& scene);

    /**
     * Registers `onUpdate` to run once per rendered frame, on the engine's
     * shared simulation coordinator (see EngineLoop), receiving accumulated time
     * since the previous frame in seconds. This Game keeps ticking even
     * without a window attached; call again to replace the callback, or
     * pass an empty std::function to stop being ticked.
     */
    void OnUpdate(std::function<void(float deltaTime)> onUpdate);

    /**
     * Adds `onUpdate` without replacing the callback installed by OnUpdate or
     * any other subscription. The callback uses the same render-thread timing
     * and removal barrier as OnUpdate. The returned move-only handle must be
     * retained; destroying or resetting it unsubscribes. An empty callback
     * returns an empty handle.
     */
    UpdateSubscription SubscribeUpdate(std::function<void(float deltaTime)> onUpdate);

    /** Time elapsed between the two most recently rendered frames, in seconds. */
    float DeltaTime() const noexcept;

    /** Total number of frames rendered by the shared engine loop since it started. */
    std::uint64_t FrameCount() const noexcept;

    /** Smoothed frames per second the shared engine loop is currently running at. */
    float Fps() const noexcept;

    /** Latest measured CPU phase times for the completed frame (any thread). */
    FrameStats Stats() const noexcept;

    /**
     * Blocks the calling thread until this Game's window is closed — by the
     * user clicking the title-bar ✕, or by DetachWindow. The engine keeps
     * rendering and ticking on its own thread throughout; this simply parks
     * the caller (typically main) until the window is dismissed, so an app
     * stays alive as long as its window does instead of for a fixed sleep.
     *
     * Returns immediately if no window is attached. A no-op-friendly building
     * block: pair it with AttachWindow/LoadScene, then Run() at the end of main.
     */
    void Run();

    /** Ends the engine lifecycle, releasing every subsystem it activated. */
    void Destroy();

private:
    friend class Scene;

    /** Deactivates and unlinks the current scene, if any. */
    void UnloadScene();

    /** Called by Scene's destructor so a dying scene never leaves a dangling pointer here. */
    void ForgetScene(Scene* scene);

    GameConfig m_config;
    std::shared_ptr<EngineLoop> m_loop;
    EngineLoop::WindowId m_windowId = EngineLoop::kInvalidWindowId;
    /** Live Window handle for BindLive / clear-on-detach; non-owning. */
    Window* m_liveWindow = nullptr;
    EngineLoop::UpdateId m_updateId = EngineLoop::kInvalidUpdateId;
    std::vector<std::weak_ptr<UpdateSubscription::State>> m_subscriptions;
    Scene* m_activeScene = nullptr;
    bool m_destroyed = false;
};

} // namespace Concord

#endif // CONCORD_GAME_H
