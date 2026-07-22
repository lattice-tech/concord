#ifndef CONCORD_ACTIONID_H
#define CONCORD_ACTIONID_H

#include <functional>
#include <string>
#include <string_view>

namespace Concord {

/**
 * @brief Named digital action (press / hold / release).
 *
 * Compared by string equality. Prefer short stable names such as "Jump"
 * or "UI.Confirm" so bindings stay readable in config and code.
 */
struct ActionId {
    std::string name;

    ActionId() = default;
    explicit ActionId(std::string_view actionName) : name(actionName) {}

    friend bool operator==(const ActionId&, const ActionId&) noexcept = default;
};

} // namespace Concord

namespace std {

template <>
struct hash<Concord::ActionId> {
    size_t operator()(const Concord::ActionId& id) const noexcept
    {
        return hash<string>{}(id.name);
    }
};

} // namespace std

#endif // CONCORD_ACTIONID_H
