#include "streaming/PackedChunkVertex.hpp"

namespace betacraft::streaming {

// Static asserts to guarantee POD invariants
static_assert(sizeof(PackedChunkVertex) == 8, "PackedChunkVertex must be exactly 8 bytes");
static_assert(alignof(PackedChunkVertex) == 8, "PackedChunkVertex must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<PackedChunkVertex>, "PackedChunkVertex must be trivially copyable");

} // namespace betacraft::streaming
