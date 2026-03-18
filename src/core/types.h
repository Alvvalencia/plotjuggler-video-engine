#ifndef VIDEOENGINE_CORE_TYPES_H
#define VIDEOENGINE_CORE_TYPES_H

#include <cstdint>

namespace videoengine {

using Timestamp = int64_t; // microseconds
using Duration = int64_t;  // microseconds

enum class Codec {
    H264,
    H265,
    AV1,
    Unknown
};

} // namespace videoengine

#endif // VIDEOENGINE_CORE_TYPES_H
