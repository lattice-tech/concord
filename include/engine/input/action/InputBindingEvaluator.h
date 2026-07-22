#ifndef CONCORD_INPUTBINDINGEVALUATOR_H
#define CONCORD_INPUTBINDINGEVALUATOR_H

#include "engine/input/action/InputBinding.h"

#include <array>
#include <cstddef>

namespace Concord {

class InputState;

namespace InputDetail {

/** Physical input sources already owned by a higher-priority context. */
struct BindingSources {
    std::array<bool, static_cast<std::size_t>(Key::Count)> keys{};
    std::array<bool, static_cast<std::size_t>(MouseButton::Count)> buttons{};
    bool mouseDeltaX = false;
    bool mouseDeltaY = false;

    /** True when at least one physical source is represented. */
    bool Any() const noexcept;

    /** Adds every source represented by @p other to this set. */
    void Merge(const BindingSources& other) noexcept;
};

/** Held state and physical transition edges observed for one digital binding. */
struct ActionBindingSample {
    bool down = false;
    bool pressed = false;
    bool released = false;

    /** True when the binding produced held state or an edge this frame. */
    bool Any() const noexcept { return down || pressed || released; }
};

/** Samples one digital binding while ignoring unavailable physical sources. */
ActionBindingSample SampleActionBinding(const ActionBinding& binding,
                                        const InputState& input,
                                        const BindingSources& unavailable,
                                        BindingSources& used);

/** Samples one axis binding while ignoring unavailable physical sources. */
float SampleAxisBinding(const AxisBinding& binding, const InputState& input,
                        const BindingSources& unavailable, BindingSources& used);

} // namespace InputDetail
} // namespace Concord

#endif // CONCORD_INPUTBINDINGEVALUATOR_H
