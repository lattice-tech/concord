#ifndef CONCORD_REFLECTIONRECEIVERSELECTOR_H
#define CONCORD_REFLECTIONRECEIVERSELECTOR_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace Concord {

/** World bounds and identity of one reflective draw-command candidate. */
struct ReflectionReceiverBounds {
    /** Equality-only node identity; zero means this draw is an independent legacy receiver. */
    std::uintptr_t owner = 0;

    /** Index of the corresponding command in the backend's pending draw array. */
    std::size_t commandIndex = 0;

    /** World-space axis-aligned minimum and maximum. */
    float boundsMin[3]{};
    float boundsMax[3]{};
};

/** Dominant reflective node selected to own a window's shared scene probe. */
struct ReflectionReceiverSelection {
    /** Equality-only node identity, or zero for an independent legacy command. */
    std::uintptr_t owner = 0;

    /** First command in the selected group, used when owner is zero. */
    std::size_t representativeIndex = 0;

    /** Union of every selected sub-mesh's world bounds. */
    float boundsMin[3]{};
    float boundsMax[3]{};

    bool valid = false;
};

/**
 * Unions candidates with the same non-zero owner and selects the group with
 * the largest world-space half extent. This preserves the historical
 * largest-receiver policy while treating a multi-sub-mesh model as one object.
 */
ReflectionReceiverSelection SelectReflectionReceiver(
    std::span<const ReflectionReceiverBounds> candidates);

/** Returns true when a command belongs to @p selection and must be omitted from its capture. */
bool IsSelectedReflectionReceiver(const ReflectionReceiverSelection& selection,
                                  std::uintptr_t owner,
                                  std::size_t commandIndex) noexcept;

} // namespace Concord

#endif // CONCORD_REFLECTIONRECEIVERSELECTOR_H
