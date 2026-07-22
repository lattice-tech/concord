#ifndef CONCORD_OBJECTID_H
#define CONCORD_OBJECTID_H

#include <cstdint>

namespace Concord::Object {

using ObjectId = std::uint64_t;
inline constexpr ObjectId kInvalidObjectId = 0;

} // namespace Concord::Object

#endif // CONCORD_OBJECTID_H
