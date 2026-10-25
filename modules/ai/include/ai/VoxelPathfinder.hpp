#pragma once

#include "ai/NavVoxelQuery.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <queue>
#include <span>
#include <unordered_map>
#include <vector>

namespace betacraft::ai {

// Voxel A* for walking mobs.
//
// A node is the cell of the mob's feet, i.e. the corner cell of its footprint: ceil(extents.x)
// x ceil(extents.z) columns towards +X/+Z, ceil(extents.y) cells tall (a zombie 1x1x2, a
// spider 2x2x1, like Beta's PathFinder which rounds the entity size up). A node is standable
// when every footprint cell up to the mob's height is free (Passable, Liquid, Ladder, open
// door, closed door for canOpenDoors) and one footprint column stands on Solid or Ladder or
// has its feet in Ladder or Liquid; no footprint cell below may be a Hazard.
//
// Moves (canTraverse), in the order they are tried from a node:
//   * the 8 horizontal neighbours (cost 1, sqrt 2 diagonally). A diagonal is allowed only when
//     both orthogonal neighbours are free over the mob's height: no corner cutting;
//   * a 1-block step up, orthogonal (cost 1.5), with one free cell above the mob's head;
//   * a drop of 1 to 3 blocks, orthogonal, onto the first floor below (cost 1 + 0.5/block);
//   * straight up or down a ladder or through water (cost 1).
// A climbsWalls agent (spider) is also held by any wall touching its footprint, so it plans
// straight up and down walls like a ladder.
// Hazards (lava, cactus) are never entered, and a move into Liquid costs 3 times as much.
struct NavAgent {
    glm::vec3 extents{0.6f, 1.8f, 0.6f}; // width (X), height (Y), depth (Z) in blocks
    bool canOpenDoors{false};
    bool climbsWalls{false};
};

// Footprint in cells: ceil of each extent, at least 1.
[[nodiscard]] glm::ivec3 navFootprint(const glm::vec3& extents) noexcept;

// Octile distance on X/Z (Y ignored): max + (sqrt 2 - 1) * min. Admissible and consistent
// for the moves above since every move costs at least its horizontal octile length.
[[nodiscard]] float octileDistance2D(const glm::ivec3& a, const glm::ivec3& b) noexcept;

[[nodiscard]] bool isStandable(const INavVoxelQuery& query, const glm::ivec3& node, const NavAgent& agent);

// Cost of the single move from -> to, or nullopt when it is not one of the moves above.
[[nodiscard]] std::optional<float> moveCost(const INavVoxelQuery& query, const glm::ivec3& from,
                                            const glm::ivec3& to, const NavAgent& agent);

[[nodiscard]] bool canTraverse(const INavVoxelQuery& query, const glm::ivec3& from, const glm::ivec3& to,
                               const glm::vec3& extents, bool canOpenDoors = false);

enum class SearchStatus : uint8_t {
    InProgress, // step() again
    Found,      // path() ends at the goal
    Partial,    // goal unreachable or node cap hit: path() ends at the node closest to it
    Failed      // start not loaded: path() is empty
};

// Time-sliced A*: each step() expands at most `nodeBudget` nodes, so a search spreads over
// several ticks (PathPlanningSystem gives each NODES_PER_TICK). Expansions over the whole
// search are capped at `maxNodes`. Deterministic: ties go to the lower h, then to the node
// discovered first, and neighbours are always generated in the same order.
class PathSearch {
public:
    static constexpr int32_t NODES_PER_TICK = 250;
    static constexpr int32_t DEFAULT_MAX_NODES = 2048;

    PathSearch(const glm::ivec3& start, const glm::ivec3& goal, const NavAgent& agent,
               int32_t maxNodes = DEFAULT_MAX_NODES);

    SearchStatus step(const INavVoxelQuery& query, int32_t nodeBudget);

    // Cells from the start (included) to the goal or the best partial node; empty while
    // InProgress or after Failed.
    [[nodiscard]] std::span<const glm::ivec3> path() const noexcept { return m_path; }
    [[nodiscard]] SearchStatus status() const noexcept { return m_status; }
    [[nodiscard]] bool done() const noexcept { return m_status != SearchStatus::InProgress; }
    [[nodiscard]] int32_t expandedNodes() const noexcept { return m_expanded; }
    [[nodiscard]] const glm::ivec3& start() const noexcept { return m_start; }
    [[nodiscard]] const glm::ivec3& goal() const noexcept { return m_goal; }

private:
    struct Node {
        glm::ivec3 pos{0};
        float g{0.0f};
        int32_t parent{-1};
        bool closed{false};
    };
    struct OpenEntry {
        float f;
        float h;
        uint32_t seq; // insertion order, for determinism
        float g;
        int32_t node;
    };
    struct OpenOrder {
        bool operator()(const OpenEntry& a, const OpenEntry& b) const noexcept {
            if (a.f != b.f) return a.f > b.f;
            if (a.h != b.h) return a.h > b.h;
            return a.seq > b.seq;
        }
    };

    [[nodiscard]] static uint64_t keyOf(const glm::ivec3& p) noexcept;
    [[nodiscard]] float closeness(const glm::ivec3& p) const noexcept;
    void push(const glm::ivec3& pos, float g, int32_t parent);
    void finish(SearchStatus status, int32_t endNode);

    glm::ivec3 m_start;
    glm::ivec3 m_goal;
    NavAgent m_agent;
    int32_t m_maxNodes;
    std::vector<Node> m_nodes;
    std::unordered_map<uint64_t, int32_t> m_index;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, OpenOrder> m_open;
    uint32_t m_seq{0};
    int32_t m_expanded{0};
    int32_t m_best{0};
    bool m_started{false};
    SearchStatus m_status{SearchStatus::InProgress};
    std::vector<glm::ivec3> m_path;
};

struct PathResult {
    SearchStatus status{SearchStatus::Failed};
    std::vector<glm::ivec3> path;
    int32_t expandedNodes{0};
};

// Runs a PathSearch to completion in one call.
[[nodiscard]] PathResult findPath(const INavVoxelQuery& query, const glm::ivec3& start, const glm::ivec3& goal,
                                  const NavAgent& agent, int32_t maxNodes = PathSearch::DEFAULT_MAX_NODES);

} // namespace betacraft::ai
