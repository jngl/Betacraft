#pragma once

#include "ecs/Registry.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

namespace betacraft::ecs {

// Deferred structural changes (create/destroy entities, add/remove components).
//
// The component references View::each and Group::each hand to their callback point into the
// pools' std::vector storage. A structural change made inside the callback can invalidate them:
// an emplace into an iterated pool may reallocate it, an entity joining or leaving an owning
// group swaps payloads inside the owned pools, and destroying an entity swap-and-pops the last
// payload of every pool it was in. Recording the change here and calling flush() after the loop
// keeps every reference valid (typical case: mob death -> loot spawn -> destroy).
//
// Commands run in recording order. Entity-targeted commands re-validate their handle when they
// run, so a command aimed at an entity that died (or whose slot was recycled) in the meantime is
// skipped instead of throwing or hitting the new occupant.
class CommandBuffer {
public:
    void destroy(EntityId e) {
        m_commands.emplace_back([e](Registry& reg) { return reg.destroy(e); });
    }

    template<ComponentType T>
        requires std::is_copy_constructible_v<T>
    void emplaceOrReplace(EntityId e, T component) {
        m_commands.emplace_back([e, c = std::move(component)](Registry& reg) {
            if (!reg.valid(e)) {
                return false;
            }
            (void)reg.emplaceOrReplace<T>(e, c);
            return true;
        });
    }

    template<ComponentType T>
    void remove(EntityId e) {
        m_commands.emplace_back([e](Registry& reg) { return reg.remove<T>(e); });
    }

    // Arbitrary deferred work: fn(Registry&) -> void (always counts as applied) or -> bool.
    template<typename Fn>
        requires std::is_invocable_v<Fn&, Registry&>
    void defer(Fn&& fn) {
        m_commands.emplace_back([f = std::forward<Fn>(fn)](Registry& reg) mutable {
            if constexpr (std::is_same_v<std::invoke_result_t<Fn&, Registry&>, bool>) {
                return f(reg);
            } else {
                f(reg);
                return true;
            }
        });
    }

    [[nodiscard]] size_t size() const noexcept { return m_commands.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_commands.empty(); }

    // Applies and drains the pending commands; returns how many had an effect. Commands
    // recorded while flushing (from a deferred function) are kept for the next flush, so a
    // self-feeding command cannot loop forever.
    size_t flush(Registry& reg) {
        std::vector<Command> pending;
        pending.swap(m_commands);
        size_t applied = 0;
        for (Command& cmd : pending) {
            applied += cmd(reg) ? 1U : 0U;
        }
        return applied;
    }

    void clear() noexcept { m_commands.clear(); }

private:
    using Command = std::function<bool(Registry&)>;
    std::vector<Command> m_commands;
};

} // namespace betacraft::ecs
