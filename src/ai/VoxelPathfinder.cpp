#include "ai/VoxelPathfinder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace betacraft::ai {

namespace {

constexpr float kSqrt2 = 1.41421356f;
constexpr float kStepUpCost = 1.5f;
constexpr float kDropCostPerBlock = 0.5f;
constexpr float kLiquidFactor = 3.0f;
constexpr int32_t kMaxDrop = 3;

// Horizontal neighbours: orthogonals first, then diagonals, in a fixed order.
constexpr std::array<glm::ivec2, 8> kDirections{{
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
}};

[[nodiscard]] core::BlockPos toBlock(const glm::ivec3& p) noexcept {
    return {p.x, p.y, p.z};
}

// Whether a mob's body may occupy the cell.
[[nodiscard]] bool isFree(const INavVoxelQuery& q, const glm::ivec3& p, bool canOpenDoors) {
    if (!q.isLoaded(toBlock(p))) {
        return false;
    }
    switch (q.classify(toBlock(p))) {
        case NavCell::Passable:
        case NavCell::Liquid:
        case NavCell::Ladder:
        case NavCell::DoorOpen:
            return true;
        case NavCell::DoorClosed:
            return canOpenDoors;
        case NavCell::Solid:
        case NavCell::Hazard:
        case NavCell::Fence:
            return false;
    }
    return false;
}

// Every footprint column at `corner` free over [y0, y0 + count).
[[nodiscard]] bool columnsClear(const INavVoxelQuery& q, const glm::ivec3& corner, int32_t y0, int32_t count,
                                const glm::ivec3& fp, bool canOpenDoors) {
    for (int32_t y = y0; y < y0 + count; ++y) {
        for (int32_t dz = 0; dz < fp.z; ++dz) {
            for (int32_t dx = 0; dx < fp.x; ++dx) {
                if (!isFree(q, {corner.x + dx, y, corner.z + dz}, canOpenDoors)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Feet in a footprint cell of this kind (ladder, liquid)?
[[nodiscard]] bool feetIn(const INavVoxelQuery& q, const glm::ivec3& node, const glm::ivec3& fp, NavCell kind) {
    for (int32_t dz = 0; dz < fp.z; ++dz) {
        for (int32_t dx = 0; dx < fp.x; ++dx) {
            const core::BlockPos p{node.x + dx, node.y, node.z + dz};
            if (q.isLoaded(p) && q.classify(p) == kind) {
                return true;
            }
        }
    }
    return false;
}

// A Solid cell touches a side of the footprint at feet level (a wall a climber clings to).
[[nodiscard]] bool clinging(const INavVoxelQuery& q, const glm::ivec3& node, const glm::ivec3& fp) {
    const auto solid = [&](int32_t x, int32_t z) {
        const core::BlockPos p{x, node.y, z};
        return q.isLoaded(p) && q.classify(p) == NavCell::Solid;
    };
    for (int32_t i = 0; i < fp.x; ++i) {
        if (solid(node.x + i, node.z - 1) || solid(node.x + i, node.z + fp.z)) return true;
    }
    for (int32_t i = 0; i < fp.z; ++i) {
        if (solid(node.x - 1, node.z + i) || solid(node.x + fp.x, node.z + i)) return true;
    }
    return false;
}

// Something holds the mob up at `node` (its body cells already known free), and nothing
// under it hurts. A climber is also held by a wall beside it.
[[nodiscard]] bool isSupported(const INavVoxelQuery& q, const glm::ivec3& node, const glm::ivec3& fp,
                               bool climbsWalls) {
    bool supported = false;
    for (int32_t dz = 0; dz < fp.z; ++dz) {
        for (int32_t dx = 0; dx < fp.x; ++dx) {
            const core::BlockPos feet{node.x + dx, node.y, node.z + dz};
            const core::BlockPos below{node.x + dx, node.y - 1, node.z + dz};
            const NavCell f = q.classify(feet);
            if (f == NavCell::Ladder || f == NavCell::Liquid) {
                supported = true;
            }
            if (!q.isLoaded(below)) {
                continue;
            }
            const NavCell b = q.classify(below);
            if (b == NavCell::Hazard) {
                return false;
            }
            if (b == NavCell::Solid || b == NavCell::Ladder) {
                supported = true;
            }
        }
    }
    return supported || (climbsWalls && clinging(q, node, fp));
}

// Feet on a ladder, in water, or (climbers) against a wall: vertical moves are possible.
[[nodiscard]] bool canMoveVertically(const INavVoxelQuery& q, const glm::ivec3& node, const glm::ivec3& fp,
                                     bool climbsWalls) {
    return feetIn(q, node, fp, NavCell::Ladder) || feetIn(q, node, fp, NavCell::Liquid) ||
           (climbsWalls && clinging(q, node, fp));
}

[[nodiscard]] float arrivalFactor(const INavVoxelQuery& q, const glm::ivec3& to, const glm::ivec3& fp) {
    return feetIn(q, to, fp, NavCell::Liquid) ? kLiquidFactor : 1.0f;
}

} // namespace

glm::ivec3 navFootprint(const glm::vec3& extents) noexcept {
    auto cells = [](float e) {
        return std::isfinite(e) ? std::clamp(static_cast<int32_t>(std::ceil(e - 1e-4f)), 1, 4) : 1;
    };
    return {cells(extents.x), cells(extents.y), cells(extents.z)};
}

float octileDistance2D(const glm::ivec3& a, const glm::ivec3& b) noexcept {
    const auto dx = static_cast<float>(std::abs(static_cast<int64_t>(a.x) - b.x));
    const auto dz = static_cast<float>(std::abs(static_cast<int64_t>(a.z) - b.z));
    return std::max(dx, dz) + (kSqrt2 - 1.0f) * std::min(dx, dz);
}

bool isStandable(const INavVoxelQuery& query, const glm::ivec3& node, const NavAgent& agent) {
    const glm::ivec3 fp = navFootprint(agent.extents);
    return columnsClear(query, node, node.y, fp.y, fp, agent.canOpenDoors) &&
           isSupported(query, node, fp, agent.climbsWalls);
}

std::optional<float> moveCost(const INavVoxelQuery& q, const glm::ivec3& from, const glm::ivec3& to,
                              const NavAgent& agent) {
    const glm::ivec3 fp = navFootprint(agent.extents);
    const glm::ivec3 d = to - from;
    if (std::abs(d.x) > 1 || std::abs(d.z) > 1) {
        return std::nullopt;
    }
    const bool doors = agent.canOpenDoors;

    if (d.x == 0 && d.z == 0) {
        // Ladder, water or a climbed wall, one cell up or down.
        if (d.y == 1) {
            if (!canMoveVertically(q, from, fp, agent.climbsWalls)) {
                return std::nullopt;
            }
        } else if (d.y == -1) {
            if (!canMoveVertically(q, to, fp, agent.climbsWalls)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
        if (!isStandable(q, to, agent)) {
            return std::nullopt;
        }
        return arrivalFactor(q, to, fp);
    }

    const bool diagonal = d.x != 0 && d.z != 0;
    if (d.y == 0) {
        if (!isStandable(q, to, agent)) {
            return std::nullopt;
        }
        if (diagonal && (!columnsClear(q, {from.x + d.x, from.y, from.z}, from.y, fp.y, fp, doors) ||
                         !columnsClear(q, {from.x, from.y, from.z + d.z}, from.y, fp.y, fp, doors))) {
            return std::nullopt; // would cut the corner of a wall
        }
        return (diagonal ? kSqrt2 : 1.0f) * arrivalFactor(q, to, fp);
    }
    if (diagonal) {
        return std::nullopt;
    }
    if (d.y == 1) {
        // Jump: room above the head, then the higher cell.
        if (!columnsClear(q, from, from.y + fp.y, 1, fp, doors) || !isStandable(q, to, agent)) {
            return std::nullopt;
        }
        return kStepUpCost * arrivalFactor(q, to, fp);
    }
    const int32_t drop = -d.y;
    if (drop < 1 || drop > kMaxDrop) {
        return std::nullopt;
    }
    // Walk off the edge and fall down the column onto its first floor.
    if (!columnsClear(q, to, to.y, drop + fp.y, fp, doors) || !isSupported(q, to, fp, agent.climbsWalls)) {
        return std::nullopt;
    }
    for (int32_t y = to.y + 1; y <= from.y; ++y) {
        if (isSupported(q, {to.x, y, to.z}, fp, agent.climbsWalls)) {
            return std::nullopt; // lands higher than `to`
        }
    }
    return (1.0f + kDropCostPerBlock * static_cast<float>(drop)) * arrivalFactor(q, to, fp);
}

bool canTraverse(const INavVoxelQuery& query, const glm::ivec3& from, const glm::ivec3& to, const glm::vec3& extents,
                 bool canOpenDoors) {
    return moveCost(query, from, to, NavAgent{extents, canOpenDoors}).has_value();
}

// --- PathSearch ---------------------------------------------------------------------------

PathSearch::PathSearch(const glm::ivec3& start, const glm::ivec3& goal, const NavAgent& agent, int32_t maxNodes)
    : m_start(start), m_goal(goal), m_agent(agent), m_maxNodes(std::max(maxNodes, 1)) {
    push(start, 0.0f, -1);
}

uint64_t PathSearch::keyOf(const glm::ivec3& p) noexcept {
    // 26 bits for X and Z (Beta's border is at 3.2e7), 12 for Y.
    return ((static_cast<uint64_t>(static_cast<uint32_t>(p.x)) & 0x3FFFFFFULL) << 38U) |
           ((static_cast<uint64_t>(static_cast<uint32_t>(p.z)) & 0x3FFFFFFULL) << 12U) |
           (static_cast<uint64_t>(static_cast<uint32_t>(p.y)) & 0xFFFULL);
}

float PathSearch::closeness(const glm::ivec3& p) const noexcept {
    return octileDistance2D(p, m_goal) + static_cast<float>(std::abs(p.y - m_goal.y));
}

void PathSearch::push(const glm::ivec3& pos, float g, int32_t parent) {
    const uint64_t key = keyOf(pos);
    const auto [it, inserted] = m_index.try_emplace(key, static_cast<int32_t>(m_nodes.size()));
    if (inserted) {
        m_nodes.push_back(Node{pos, g, parent, false});
    } else {
        Node& node = m_nodes[static_cast<size_t>(it->second)];
        if (node.closed || node.g <= g) {
            return;
        }
        node.g = g;
        node.parent = parent;
    }
    const float h = octileDistance2D(pos, m_goal);
    m_open.push(OpenEntry{g + h, h, m_seq++, g, it->second});
}

void PathSearch::finish(SearchStatus status, int32_t endNode) {
    m_status = status;
    m_path.clear();
    if (status == SearchStatus::Failed) {
        return;
    }
    for (int32_t i = endNode; i >= 0; i = m_nodes[static_cast<size_t>(i)].parent) {
        m_path.push_back(m_nodes[static_cast<size_t>(i)].pos);
    }
    std::reverse(m_path.begin(), m_path.end());
    m_open = {};
    m_index.clear();
    m_nodes.clear();
    m_nodes.shrink_to_fit();
}

SearchStatus PathSearch::step(const INavVoxelQuery& query, int32_t nodeBudget) {
    if (m_status != SearchStatus::InProgress) {
        return m_status;
    }
    if (!m_started) {
        m_started = true;
        if (!query.isLoaded(toBlock(m_start))) {
            finish(SearchStatus::Failed, -1);
            return m_status;
        }
    }
    for (int32_t spent = 0; spent < nodeBudget;) {
        if (m_open.empty() || m_expanded >= m_maxNodes) {
            finish(SearchStatus::Partial, m_best);
            return m_status;
        }
        const OpenEntry top = m_open.top();
        m_open.pop();
        Node& current = m_nodes[static_cast<size_t>(top.node)];
        if (current.closed || top.g > current.g) {
            continue; // stale entry
        }
        current.closed = true;
        ++m_expanded;
        ++spent;
        const glm::ivec3 pos = current.pos;
        const float g = current.g;
        if (pos == m_goal) {
            finish(SearchStatus::Found, top.node);
            return m_status;
        }
        const Node& best = m_nodes[static_cast<size_t>(m_best)];
        const float c = closeness(pos);
        const float bc = closeness(best.pos);
        if (c < bc || (c == bc && g < best.g)) {
            m_best = top.node;
        }

        auto tryMove = [&](const glm::ivec3& to) {
            if (const std::optional<float> cost = moveCost(query, pos, to, m_agent)) {
                push(to, g + *cost, top.node);
                return true;
            }
            return false;
        };
        for (size_t i = 0; i < kDirections.size(); ++i) {
            const glm::ivec3 flat{pos.x + kDirections[i].x, pos.y, pos.z + kDirections[i].y};
            if (tryMove(flat) || i >= 4) {
                continue; // steps and drops are orthogonal only
            }
            if (tryMove(flat + glm::ivec3(0, 1, 0))) {
                continue;
            }
            for (int32_t k = 1; k <= kMaxDrop; ++k) {
                if (tryMove(flat - glm::ivec3(0, k, 0))) {
                    break;
                }
            }
        }
        (void)tryMove(pos + glm::ivec3(0, 1, 0));
        (void)tryMove(pos - glm::ivec3(0, 1, 0));
    }
    return m_status;
}

PathResult findPath(const INavVoxelQuery& query, const glm::ivec3& start, const glm::ivec3& goal, const NavAgent& agent,
                    int32_t maxNodes) {
    PathSearch search(start, goal, agent, maxNodes);
    while (search.step(query, std::numeric_limits<int32_t>::max()) == SearchStatus::InProgress) {
    }
    return PathResult{search.status(), std::vector<glm::ivec3>(search.path().begin(), search.path().end()),
                      search.expandedNodes()};
}

} // namespace betacraft::ai
