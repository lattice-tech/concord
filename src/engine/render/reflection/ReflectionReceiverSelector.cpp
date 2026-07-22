#include "engine/render/reflection/ReflectionReceiverSelector.h"

#include <algorithm>

namespace Concord {

ReflectionReceiverSelection SelectReflectionReceiver(
    std::span<const ReflectionReceiverBounds> candidates)
{
    ReflectionReceiverSelection selected;
    float largestRadius = -1.0f;
    for (std::size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
        const ReflectionReceiverBounds& candidate = candidates[candidateIndex];
        if (candidate.owner != 0) {
            const auto previous = std::find_if(
                candidates.begin(), candidates.begin() + candidateIndex,
                [&](const ReflectionReceiverBounds& existing) {
                    return existing.owner == candidate.owner;
                });
            if (previous != candidates.begin() + candidateIndex) {
                continue;
            }
        }

        ReflectionReceiverSelection group;
        group.owner = candidate.owner;
        group.representativeIndex = candidate.commandIndex;
        std::copy_n(candidate.boundsMin, 3, group.boundsMin);
        std::copy_n(candidate.boundsMax, 3, group.boundsMax);
        group.valid = true;
        if (group.owner != 0) {
            for (std::size_t otherIndex = candidateIndex + 1;
                 otherIndex < candidates.size(); ++otherIndex) {
                const ReflectionReceiverBounds& other = candidates[otherIndex];
                if (other.owner != group.owner) {
                    continue;
                }
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    group.boundsMin[axis] = std::min(group.boundsMin[axis], other.boundsMin[axis]);
                    group.boundsMax[axis] = std::max(group.boundsMax[axis], other.boundsMax[axis]);
                }
            }
        }

        const float halfExtentX = (group.boundsMax[0] - group.boundsMin[0]) * 0.5f;
        const float halfExtentY = (group.boundsMax[1] - group.boundsMin[1]) * 0.5f;
        const float halfExtentZ = (group.boundsMax[2] - group.boundsMin[2]) * 0.5f;
        const float radius = std::max(halfExtentX, std::max(halfExtentY, halfExtentZ));
        if (radius > largestRadius) {
            largestRadius = radius;
            selected = group;
        }
    }
    return selected;
}

bool IsSelectedReflectionReceiver(const ReflectionReceiverSelection& selection,
                                  std::uintptr_t owner,
                                  std::size_t commandIndex) noexcept
{
    if (!selection.valid) {
        return false;
    }
    return selection.owner != 0
        ? owner == selection.owner
        : commandIndex == selection.representativeIndex;
}

} // namespace Concord
