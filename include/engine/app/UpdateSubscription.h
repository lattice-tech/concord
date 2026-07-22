#ifndef CONCORD_UPDATESUBSCRIPTION_H
#define CONCORD_UPDATESUBSCRIPTION_H

#include "Concord/CExport.h"
#include "engine/loop/EngineLoop.h"

#include <atomic>
#include <memory>
#include <utility>

namespace Concord {

class Game;

/**
 * @brief Move-only ownership handle for an additive Game update callback.
 *
 * Destroying or resetting the handle unregisters its callback and observes the
 * same execution barrier as Game::OnUpdate replacement. The originating Game
 * also unregisters the callback during Destroy(), so retaining a handle cannot
 * extend either the Game or the process-wide EngineLoop lifetime.
 */
class CENGINE_API UpdateSubscription {
public:
    /** @brief Constructs an empty subscription. */
    UpdateSubscription() noexcept = default;

    /** @brief Unsubscribes when this handle still owns a registration. */
    ~UpdateSubscription();

    UpdateSubscription(const UpdateSubscription&) = delete;
    UpdateSubscription& operator=(const UpdateSubscription&) = delete;

    /** @brief Transfers subscription ownership from `other`. */
    UpdateSubscription(UpdateSubscription&& other) noexcept;

    /** @brief Replaces this registration with the one owned by `other`. */
    UpdateSubscription& operator=(UpdateSubscription&& other) noexcept;

    /** @brief Unregisters the callback immediately; a no-op when already empty. */
    void Reset();

    /** @brief Returns true while this handle names a registered callback. */
    explicit operator bool() const noexcept;

private:
    friend class Game;

    struct State {
        State(std::weak_ptr<EngineLoop> updateLoop, EngineLoop::UpdateId updateId) noexcept
            : loop(std::move(updateLoop))
            , id(updateId)
        {
        }

        void Reset();

        std::weak_ptr<EngineLoop> loop;
        std::atomic<EngineLoop::UpdateId> id{EngineLoop::kInvalidUpdateId};
    };

    explicit UpdateSubscription(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> m_state;
};

} // namespace Concord

#endif // CONCORD_UPDATESUBSCRIPTION_H
