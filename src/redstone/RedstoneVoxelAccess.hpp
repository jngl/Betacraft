#pragma once

#include "core/Types.hpp"
#include "world/Block.hpp"
#include "world/BlockShapes.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace betacraft::redstone {

// S8.6 voxel view of the redstone engine, on the IFluidVoxelAccess pattern: the engine never
// sees World, so it runs on a small in-memory grid in the unit tests and on a World adapter in
// the game (World::RedstoneAccess).
//
// Contract:
//  - Unloaded cells (outside the world, unloaded chunks) report isLoaded() == false, read as
//    Air with metadata 0 and ignore writes.
//  - setBlock() writes a block together with its metadata; setMetadata() keeps the block.
//    Both are ordinary edits for the owner (relighting, saving, re-meshing).
//  - pushPiston() runs the piston at `pos` towards `dir` (World::triggerPistonPush); the
//    blocks it moves come back to the engine as onBlockChanged notifications.
class IRedstoneVoxelAccess {
public:
    IRedstoneVoxelAccess() = default;
    virtual ~IRedstoneVoxelAccess() = default;

    IRedstoneVoxelAccess(const IRedstoneVoxelAccess&) = delete;
    IRedstoneVoxelAccess& operator=(const IRedstoneVoxelAccess&) = delete;

    [[nodiscard]] virtual bool isLoaded(const core::BlockPos& pos) const noexcept = 0;
    [[nodiscard]] virtual world::BlockId getBlock(const core::BlockPos& pos) const noexcept = 0;
    [[nodiscard]] virtual uint8_t getMetadata(const core::BlockPos& pos) const noexcept = 0;
    virtual void setBlock(const core::BlockPos& pos, world::BlockId id, uint8_t meta) = 0;
    virtual void setMetadata(const core::BlockPos& pos, uint8_t meta) = 0;
    virtual bool pushPiston(const core::BlockPos& pos, core::FaceDir dir) = 0;

    // Beta World.isBlockNormalCube: what conducts strong power and blocks wire steps.
    [[nodiscard]] virtual bool isNormalCube(const core::BlockPos& pos) const noexcept {
        return world::isNormalCube(getBlock(pos));
    }
};

// Dense in-memory box [0,sx) x [0,sy) x [0,sz) for the unit tests. Pistons only record their
// pushes.
class GridRedstoneVoxelAccess final : public IRedstoneVoxelAccess {
public:
    struct PistonPush {
        core::BlockPos pos{};
        core::FaceDir dir{core::FaceDir::PosX};
    };

    GridRedstoneVoxelAccess(int32_t sizeX, int32_t sizeY, int32_t sizeZ)
        : m_sx(sizeX), m_sy(sizeY), m_sz(sizeZ),
          m_cells(static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY) * static_cast<size_t>(sizeZ)) {}

    [[nodiscard]] bool isLoaded(const core::BlockPos& p) const noexcept override {
        return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < m_sx && p.y < m_sy && p.z < m_sz;
    }
    [[nodiscard]] world::BlockId getBlock(const core::BlockPos& p) const noexcept override {
        return isLoaded(p) ? m_cells[indexOf(p)].block : world::BlockId::Air;
    }
    [[nodiscard]] uint8_t getMetadata(const core::BlockPos& p) const noexcept override {
        return isLoaded(p) ? m_cells[indexOf(p)].meta : uint8_t{0};
    }
    void setBlock(const core::BlockPos& p, world::BlockId id, uint8_t meta) override {
        if (isLoaded(p)) {
            m_cells[indexOf(p)] = {id, static_cast<uint8_t>(meta & 0x0F)};
            ++m_writes;
        }
    }
    void setMetadata(const core::BlockPos& p, uint8_t meta) override {
        if (isLoaded(p)) {
            m_cells[indexOf(p)].meta = static_cast<uint8_t>(meta & 0x0F);
            ++m_writes;
        }
    }
    bool pushPiston(const core::BlockPos& p, core::FaceDir dir) override {
        m_pushes.push_back({p, dir});
        return true;
    }

    // Inclusive box fill (clipped).
    void fill(const core::BlockPos& from, const core::BlockPos& to, world::BlockId id, uint8_t meta = 0) {
        for (int32_t y = from.y; y <= to.y; ++y)
            for (int32_t z = from.z; z <= to.z; ++z)
                for (int32_t x = from.x; x <= to.x; ++x) setBlock({x, y, z}, id, meta);
    }

    [[nodiscard]] const std::vector<PistonPush>& pistonPushes() const noexcept { return m_pushes; }
    [[nodiscard]] uint64_t writeCount() const noexcept { return m_writes; }

private:
    struct Cell {
        world::BlockId block{world::BlockId::Air};
        uint8_t meta{0};
    };

    [[nodiscard]] size_t indexOf(const core::BlockPos& p) const noexcept {
        return (static_cast<size_t>(p.y) * static_cast<size_t>(m_sz) + static_cast<size_t>(p.z)) *
                   static_cast<size_t>(m_sx) +
               static_cast<size_t>(p.x);
    }

    int32_t m_sx;
    int32_t m_sy;
    int32_t m_sz;
    std::vector<Cell> m_cells;
    std::vector<PistonPush> m_pushes;
    uint64_t m_writes{0};
};

} // namespace betacraft::redstone
