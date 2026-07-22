#ifndef CONCORD_ANIMATIONTRANSITION_H
#define CONCORD_ANIMATIONTRANSITION_H

#include "engine/animation/AnimationParameters.h"

#include <string>
#include <vector>

namespace Concord::Animation {

/**
 * One test on a single parameter. A transition fires only when *all* of its
 * conditions pass (logical AND); model an OR by adding two transitions between
 * the same states.
 */
struct TransitionCondition {
    enum class Op {
        Greater,   ///< float parameter > value
        Less,      ///< float parameter < value
        IsTrue,    ///< bool parameter is true
        IsFalse,   ///< bool parameter is false
        Trigger,   ///< trigger parameter is set (consumed when the transition fires)
    };

    std::string parameter;
    Op op = Op::IsTrue;
    float value = 0.0f; ///< comparison value for Greater/Less; ignored otherwise

    /** Evaluates this condition against the current parameter values. */
    bool Evaluate(const AnimationParameters& params) const
    {
        switch (op) {
            case Op::Greater: return params.GetFloat(parameter) > value;
            case Op::Less:    return params.GetFloat(parameter) < value;
            case Op::IsTrue:  return params.GetBool(parameter);
            case Op::IsFalse: return !params.GetBool(parameter);
            case Op::Trigger: return params.GetTrigger(parameter);
        }
        return false;
    }

    bool IsTrigger() const noexcept { return op == Op::Trigger; }
};

/**
 * A directed edge in the state graph: leave `from` for `to` once every
 * condition passes, cross-fading over `duration` seconds. `from` empty means
 * an "any state" transition (fires from whatever state is current), the usual
 * way to wire a hit/death reaction reachable from everywhere.
 */
struct AnimationTransition {
    std::string from; ///< source state name; empty = any state
    std::string to;   ///< destination state name
    std::vector<TransitionCondition> conditions;
    float duration = 0.15f; ///< crossfade time in seconds (0 = instant cut)
};

} // namespace Concord::Animation

#endif // CONCORD_ANIMATIONTRANSITION_H
