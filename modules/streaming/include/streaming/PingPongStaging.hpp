#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace betacraft::streaming {

// Double-buffered (ping-pong) hand-off between many producer threads (mesher workers)
// and exactly ONE consumer thread (the render thread).
//
// Producers append to the back buffer under a short mutex. The consumer drains its own
// front buffer without any lock; only when the front is exhausted does it take the lock
// once to swap the two vectors (O(1) pointer exchange). The drained front's capacity is
// recycled as the next back buffer, so steady-state streaming does no reallocation.
// Items are delivered in global publication order (FIFO), which keeps the priority order
// produced by the scheduler.
template <typename T>
class PingPongStaging {
public:
    PingPongStaging() = default;
    PingPongStaging(const PingPongStaging&) = delete;
    PingPongStaging& operator=(const PingPongStaging&) = delete;

    // ---- producer side (any thread) ---------------------------------------------
    void publish(T item) {
        const std::lock_guard lock(m_backMutex);
        m_back.push_back(std::move(item));
    }

    void publishBatch(std::vector<T>&& items) {
        const std::lock_guard lock(m_backMutex);
        if (m_back.empty()) {
            m_back.swap(items);
        } else {
            for (T& item : items) {
                m_back.push_back(std::move(item));
            }
        }
        items.clear();
    }

    [[nodiscard]] size_t backSize() const {
        const std::lock_guard lock(m_backMutex);
        return m_back.size();
    }

    // ---- consumer side (single thread only) -------------------------------------
    // Returns the next item without consuming it, or nullptr when both buffers are empty.
    [[nodiscard]] T* peek() {
        if (m_cursor >= m_front.size() && !swapBuffers()) {
            return nullptr;
        }
        return &m_front[m_cursor];
    }

    // Consumes the item returned by the last successful peek(); no-op when empty.
    void pop() noexcept {
        if (m_cursor < m_front.size()) {
            ++m_cursor;
        }
    }

    [[nodiscard]] bool tryPop(T& out) {
        T* next = peek();
        if (next == nullptr) {
            return false;
        }
        out = std::move(*next);
        pop();
        return true;
    }

    // Drops everything (e.g. world reload). Consumer thread only.
    void clear() {
        m_front.clear();
        m_cursor = 0;
        const std::lock_guard lock(m_backMutex);
        m_back.clear();
    }

    [[nodiscard]] size_t frontRemaining() const noexcept { return m_front.size() - m_cursor; }
    [[nodiscard]] uint64_t swapCount() const noexcept { return m_swapCount; }

private:
    bool swapBuffers() {
        m_front.clear(); // keeps capacity: recycled as the producers' next back buffer
        m_cursor = 0;
        {
            const std::lock_guard lock(m_backMutex);
            if (m_back.empty()) {
                return false;
            }
            m_front.swap(m_back);
        }
        ++m_swapCount;
        return true;
    }

    mutable std::mutex m_backMutex;
    std::vector<T> m_back;  // guarded by m_backMutex
    std::vector<T> m_front; // consumer-owned
    size_t m_cursor{0};     // consumer-owned: next unread index in m_front
    uint64_t m_swapCount{0};
};

} // namespace betacraft::streaming
