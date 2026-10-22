#ifndef CORE_LIBS_PRX_LIBKERNEL_FILE_INCLUDE_TRANSFERLIMITS_HPP
#define CORE_LIBS_PRX_LIBKERNEL_FILE_INCLUDE_TRANSFERLIMITS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace File {

constexpr bool ExceedsTransferLimit(std::uint64_t bytes) {
    return bytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

constexpr unsigned int NativeIoChunk(std::size_t bytes) {
    return static_cast<unsigned int>(std::min(bytes, static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

}

#endif
