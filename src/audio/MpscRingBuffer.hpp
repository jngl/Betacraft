#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace betacraft::audio {

template<typename T, size_t Capacity = 512>
class alignas(64) MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t BufferMask = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence{0};
        T data{};
    };

public:
    MpscRingBuffer() noexcept {
        for (size_t i = 0; i < Capacity; ++i) {
            m_buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MpscRingBuffer() noexcept = default;

    MpscRingBuffer(const MpscRingBuffer&) = delete;
    MpscRingBuffer& operator=(const MpscRingBuffer&) = delete;
    MpscRingBuffer(MpscRingBuffer&&) = delete;
    MpscRingBuffer& operator=(MpscRingBuffer&&) = delete;

    [[nodiscard]] bool push(const T& item) noexcept {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = m_buffer[tail & BufferMask];
            const size_t seq = cell.sequence.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail);

            if (dif == 0) {
                if (m_tail.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed)) {
                    cell.data = item;
                    cell.sequence.store(tail + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false; // Queue full
            } else {
                tail = m_tail.load(std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] bool pop(T& item) noexcept {
        Cell& cell = m_buffer[m_head & BufferMask];
        const size_t seq = cell.sequence.load(std::memory_order_acquire);
        const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(m_head + 1);

        if (dif == 0) {
            item = std::move(cell.data);
            cell.sequence.store(m_head + Capacity, std::memory_order_release);
            m_head = (m_head + 1);
            return true;
        }
        return false; // Queue empty
    }

    [[nodiscard]] bool empty() const noexcept {
        const Cell& cell = m_buffer[m_head & BufferMask];
        const size_t seq = cell.sequence.load(std::memory_order_relaxed);
        return (static_cast<intptr_t>(seq) - static_cast<intptr_t>(m_head + 1)) != 0;
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept {
        return Capacity;
    }

private:
    alignas(64) std::array<Cell, Capacity> m_buffer;
    alignas(64) std::atomic<size_t> m_tail{0};
    alignas(64) size_t m_head{0};
};

} // namespace betacraft::audio
