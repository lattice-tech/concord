#ifndef CONCORD_AXISID_H
#define CONCORD_AXISID_H

#include <functional>
#include <string>
#include <string_view>

namespace Concord {

/**
 * @brief Named analog axis (e.g. MoveX, LookY).
 *
 * Value is typically in [-1, 1] after deadzone and sensitivity, unless a
 * binding scales it further.
 */
struct AxisId {
    std::string name;

    AxisId() = default;
    explicit AxisId(std::string_view axisName) : name(axisName) {}

    friend bool operator==(const AxisId&, const AxisId&) noexcept = default;
};

} // namespace Concord

namespace std {

template <>
struct hash<Concord::AxisId> {
    size_t operator()(const Concord::AxisId& id) const noexcept
    {
        return hash<string>{}(id.name);
    }
};

} // namespace std

#endif // CONCORD_AXISID_H
