#ifndef CONCORD_SCREENEFFECTSTACK_H
#define CONCORD_SCREENEFFECTSTACK_H

#include "Concord/CExport.h"
#include "engine/effects/view/LensFlareDesc.h"
#include "engine/effects/view/MagnifierEffectDesc.h"
#include "engine/effects/view/ScreenShakeDesc.h"
#include "engine/render/frame/ViewEffectState.h"

#include <mutex>
#include <vector>

namespace Concord::Object {
class Camera;
}

namespace Concord::Effects {

/**
 * @brief Thread-safe runtime controller for one Camera's screen effects.
 *
 * Multiple shakes may overlap and add together. Magnifier state is singular:
 * setting it replaces the previous lens. Scene advances the stack for the
 * active Camera, while callers may safely trigger or query effects from other
 * threads.
 */
class CENGINE_API ScreenEffectStack {
public:
    /** Initializes an empty stack with a disabled default magnifier. */
    ScreenEffectStack();

    /** Releases transient effect storage. */
    ~ScreenEffectStack();

    ScreenEffectStack(const ScreenEffectStack&) = delete;
    ScreenEffectStack& operator=(const ScreenEffectStack&) = delete;

    /**
     * Starts another shake after validating and clamping its parameters.
     * @return true when a non-zero shake was started; false when its sanitized
     *         duration or both amplitudes are zero.
     */
    bool PlayShake(const ScreenShakeDesc& desc);

    /** Stops every active shake and resets the pixel offset to zero. */
    void ClearShakes();

    /** Returns true while at least one shake still has lifetime remaining. */
    bool IsShaking() const;

    /** Returns the current combined shake displacement in output pixels. */
    Vector2 ShakeOffsetPixels() const;

    /**
     * Advances transient effects by @p deltaTime seconds.
     *
     * An active Camera is advanced automatically by Scene. This method remains
     * public so a detached stack can be simulated deterministically in tools
     * and tests; callers must not also advance a stack owned by an active Scene.
     */
    void Advance(float deltaTime);

    /** Replaces the magnifier configuration after validating every field. */
    void SetMagnifier(const MagnifierEffectDesc& desc);

    /** Disables the magnifier while retaining its other sanitized settings. */
    void DisableMagnifier();

    /** Returns a thread-safe copy of the sanitized magnifier configuration. */
    MagnifierEffectDesc Magnifier() const;

    /** Enables the lens flare with the given sanitized parameters. */
    void SetLensFlare(const LensFlareDesc& desc);

    /** Disables the lens flare. */
    void DisableLensFlare();

    /** Returns a thread-safe copy of the sanitized lens-flare configuration. */
    LensFlareDesc LensFlare() const;

    /** Clears all shakes and restores the default disabled magnifier. */
    void Clear();

private:
    friend class Concord::Object::Camera;

    /** Runtime clock and sanitized authoring data for one active shake. */
    struct ShakeInstance {
        ScreenShakeDesc desc{};
        float elapsed = 0.0f;
    };

    /** Builds the render-thread snapshot without exposing stack internals. */
    ViewEffectState Snapshot() const;

    /** Re-evaluates every shake and writes their bounded combined offset. */
    void RebuildShakeState();

    /** Copies the sanitized magnifier into the render-thread state. */
    void ApplyMagnifierState();

    /** Copies the sanitized lens-flare enable/intensity into the render state. */
    void ApplyLensFlareState();

    mutable std::mutex m_mutex;
    std::vector<ShakeInstance> m_shakes;
    MagnifierEffectDesc m_magnifier{};
    LensFlareDesc m_lensFlare{.enabled = false, .intensity = 1.0f};
    ViewEffectState m_state{};
};

} // namespace Concord::Effects

#endif // CONCORD_SCREENEFFECTSTACK_H
