#ifndef CONCORD_INPUTACTIONS_H
#define CONCORD_INPUTACTIONS_H

#include "Concord/CExport.h"
#include "engine/input/action/ActionId.h"
#include "engine/input/action/AxisId.h"
#include "engine/input/action/InputContext.h"

#include <cstdint>
#include <string_view>

namespace Concord {

/**
 * @brief Process-wide action / axis evaluation over the context stack.
 *
 * Call sites poll after the EngineLoop has pumped input for the frame
 * (typically from Game / Scene OnUpdate). Contexts are ordered by priority
 * (descending); within the same priority, later pushes win.
 *
 * Triggered physical inputs are hidden from lower contexts even when those
 * contexts bind them to different action or axis names. Modal contexts may
 * block every lower context through `InputContext::blocksLowerContexts`.
 * Pushing or popping a context changes held state immediately but never
 * synthesizes press/release edges; modal owners cancel active gestures when
 * their context changes.
 *
 * Physical inputs are only Concord `Key` / `MouseButton` — never SDL.
 */
class CENGINE_API InputActions {
public:
    InputActions() = delete;

    /** Pushes a context copy onto the stack. Returns a handle for PopContext. */
    static std::uint64_t PushContext(InputContext context);

    /** Removes the context with `handle`, or no-ops if already gone. */
    static void PopContext(std::uint64_t handle);

    /** Removes every context (tests / shutdown). */
    static void Clear();

    /** True while any non-consumed binding for `action` is held. */
    static bool IsActionDown(const ActionId& action);
    static bool IsActionDown(std::string_view action);

    /** True on a visible binding's physical up-to-down edge (unconsumed). */
    static bool WasActionPressed(const ActionId& action);
    static bool WasActionPressed(std::string_view action);

    /** True on a visible, previously-held binding's physical release edge. */
    static bool WasActionReleased(const ActionId& action);
    static bool WasActionReleased(std::string_view action);

    /**
     * Marks `action` consumed for the rest of this frame so lower-priority
     * contexts and later queries see it as inactive.
     */
    static void Consume(const ActionId& action);
    static void Consume(std::string_view action);

    /** Combined axis value in roughly [-1, 1] after deadzone/sensitivity. */
    static float GetAxis(const AxisId& axis);
    static float GetAxis(std::string_view axis);

    /**
     * @brief EngineLoop hook: samples InputState into action/axis edges.
     *
     * Call once per frame after SDL input has been applied to InputState and
     * before Game/Scene update. Not part of the public application surface.
     */
    static void UpdateFromInputState();
};

} // namespace Concord

#endif // CONCORD_INPUTACTIONS_H
