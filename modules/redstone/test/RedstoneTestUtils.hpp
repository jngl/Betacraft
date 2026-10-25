#pragma once
// Shared bench of the redstone unit tests: a stone floor at y = 0 of an in-memory grid,
// circuits at y = 1, and an engine that drains its own scheduled tick queue.

#include "redstone/RedstoneEngine.hpp"
#include "redstone/RedstoneVoxelAccess.hpp"
#include "voxel/Block.hpp"
#include "voxel/BlockShapes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <memory>

namespace betacraft::test::redstone {

using betacraft::redstone::GridRedstoneVoxelAccess;
using betacraft::redstone::RedstoneEngine;
using betacraft::redstone::RedstoneTickStats;
using voxel::BlockId;

constexpr int32_t kY = 1; // Circuits stand on the floor

struct Bench {
    explicit Bench(int32_t sx = 32, int32_t sy = 6, int32_t sz = 32,
                   uint32_t budget = RedstoneEngine::kMaxEvaluationsPerTick)
        : grid(sx, sy, sz), engine(std::make_unique<RedstoneEngine>(grid, nullptr, budget)) {
        voxel::BlockRegistry::initialize();
        grid.fill({0, 0, 0}, {sx - 1, 0, sz - 1}, BlockId::Stone);
    }

    // Places a block and tells the engine.
    void put(const core::BlockPos& p, BlockId id, uint8_t meta = 0) {
        grid.setBlock(p, id, meta);
        engine->onBlockChanged(p);
    }
    void remove(const core::BlockPos& p) { put(p, BlockId::Air); }

    // Wire along the axis-aligned segments joining `corners` (at y = kY), each cell placed
    // silently when `notify` is false.
    void wirePath(std::initializer_list<glm::ivec2> corners, bool notify = true) {
        const glm::ivec2* prev = nullptr;
        for (const glm::ivec2& c : corners) {
            if (prev == nullptr) {
                place({c.x, kY, c.y}, notify);
            } else {
                glm::ivec2 p = *prev;
                const glm::ivec2 step{c.x > p.x ? 1 : (c.x < p.x ? -1 : 0), c.y > p.y ? 1 : (c.y < p.y ? -1 : 0)};
                while (p != c) {
                    p += step;
                    place({p.x, kY, p.y}, notify);
                }
            }
            prev = &c;
        }
    }

    RedstoneTickStats tick() { return engine->tick(++now); }

    // Runs `n` ticks; returns the number of torch switches they made.
    uint32_t run(int32_t n) {
        uint32_t toggles = 0;
        for (int32_t i = 0; i < n; ++i) toggles += tick().torchToggles;
        return toggles;
    }

    // Ticks until no work is left (scheduled updates included) or `maxTicks` ran; returns
    // the ticks spent.
    int32_t settle(int32_t maxTicks = 400) {
        for (int32_t t = 0; t < maxTicks; ++t) {
            if (engine->idle() && engine->scheduledTicks().empty()) return t;
            (void)tick();
        }
        return maxTicks;
    }

    [[nodiscard]] uint8_t power(int32_t x, int32_t z) const { return engine->powerAt({x, kY, z}); }
    [[nodiscard]] BlockId at(const core::BlockPos& p) const { return grid.getBlock(p); }
    [[nodiscard]] uint8_t meta(const core::BlockPos& p) const { return grid.getMetadata(p); }

    GridRedstoneVoxelAccess grid;
    std::unique_ptr<RedstoneEngine> engine;
    uint64_t now{0};

private:
    void place(const core::BlockPos& p, bool notify) {
        if (notify) {
            put(p, BlockId::RedstoneWire);
        } else {
            grid.setBlock(p, BlockId::RedstoneWire, 0);
        }
    }
};

// A lever standing on the floor, on or off.
inline uint8_t floorLever(bool on) {
    return static_cast<uint8_t>(voxel::kAttachFloor | (on ? voxel::kLeverOnBit : 0));
}

} // namespace betacraft::test::redstone
