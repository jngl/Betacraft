#pragma once

#include <cstddef>

namespace betacraft::lighting::std140 {

// Compile-time model of the GLSL std140 uniform block layout rules (GL 3.3 core §2.11.4).
// The C++ packer derives every byte offset from this builder instead of hand-typed numbers,
// and the GLSL contract test replays the shader's member list through the same builder, so a
// mismatch between C++ and GLSL cannot go unnoticed.

enum class Type { Float, Int, UInt, Vec2, Vec3, Vec4, IVec4, UVec4, Mat4 };

inline constexpr size_t kVec4Alignment = 16;

[[nodiscard]] constexpr size_t alignUp(size_t value, size_t alignment) noexcept {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

// Rules 1-3: scalars align to N=4, two-component vectors to 2N, three/four-component to 4N.
[[nodiscard]] constexpr size_t baseAlignment(Type type) noexcept {
    switch (type) {
        case Type::Float:
        case Type::Int:
        case Type::UInt: return 4;
        case Type::Vec2: return 8;
        case Type::Vec3:
        case Type::Vec4:
        case Type::IVec4:
        case Type::UVec4:
        case Type::Mat4: return kVec4Alignment; // Rule 5: matrix == array of column vec4
    }
    return kVec4Alignment;
}

[[nodiscard]] constexpr size_t byteSize(Type type) noexcept {
    switch (type) {
        case Type::Float:
        case Type::Int:
        case Type::UInt: return 4;
        case Type::Vec2: return 8;
        case Type::Vec3: return 12; // Only 12 bytes: a scalar may occupy the remaining 4
        case Type::Vec4:
        case Type::IVec4:
        case Type::UVec4: return 16;
        case Type::Mat4: return 64;
    }
    return 0;
}

// Rule 4: array elements are rounded up to a vec4, so `float a[N]` wastes 12 bytes per entry.
[[nodiscard]] constexpr size_t arrayStride(Type type) noexcept {
    return alignUp(byteSize(type), kVec4Alignment);
}

// Appends members in declaration order and returns each member's byte offset.
class LayoutBuilder {
public:
    constexpr size_t add(Type type) noexcept {
        const size_t offset = alignUp(m_size, baseAlignment(type));
        m_size = offset + byteSize(type);
        return offset;
    }

    constexpr size_t addArray(Type type, size_t count) noexcept {
        const size_t offset = alignUp(m_size, kVec4Alignment);
        m_size = offset + arrayStride(type) * count;
        return offset;
    }

    // Rule 9/10: structs align to vec4 and their (rounded) size is the array stride.
    constexpr size_t addStructArray(size_t structSize, size_t count) noexcept {
        const size_t offset = alignUp(m_size, kVec4Alignment);
        m_size = offset + alignUp(structSize, kVec4Alignment) * count;
        return offset;
    }

    [[nodiscard]] constexpr size_t size() const noexcept { return m_size; }

    // Size of this member list when used as a struct: padded to the struct alignment (vec4).
    [[nodiscard]] constexpr size_t structSize() const noexcept { return alignUp(m_size, kVec4Alignment); }

private:
    size_t m_size{0};
};

} // namespace betacraft::lighting::std140
