#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Grid primitives for the turn-based tactical engine.
// See turn_based_tactical_contract.md section 3.

struct GridPos {
    int16_t x = 0;
    int16_t y = 0;

    bool operator==(const GridPos& other) const { return x == other.x && y == other.y; }
    bool operator!=(const GridPos& other) const { return !(*this == other); }
};

struct GridPosHash {
    size_t operator()(const GridPos& p) const {
        return (static_cast<size_t>(static_cast<uint16_t>(p.x)) << 16) ^
                static_cast<size_t>(static_cast<uint16_t>(p.y));
    }
};

// Chebyshev distance (8-connected movement cost).
inline int chebyshev_distance(GridPos a, GridPos b) {
    int dx = a.x - b.x; if (dx < 0) dx = -dx;
    int dy = a.y - b.y; if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

// Manhattan distance (4-connected).
inline int manhattan_distance(GridPos a, GridPos b) {
    int dx = a.x - b.x; if (dx < 0) dx = -dx;
    int dy = a.y - b.y; if (dy < 0) dy = -dy;
    return dx + dy;
}

enum class CellType : uint8_t {
    FLOOR = 0,
    WALL = 1,
    COVER = 2,
};

enum class DoorState : uint8_t {
    OPEN = 0,
    CLOSED = 1,
    LOCKED = 2,
};

struct DoorEdge {
    GridPos a;          // one side (the "lower" cell by packed order)
    GridPos b;          // the other side
    DoorState state = DoorState::CLOSED;
};

const char* cell_type_str(CellType t);
const char* door_state_str(DoorState s);

// Canonical 64-bit key for an edge between two adjacent cells.
// Order-independent: edge(a,b) == edge(b,a).
uint64_t pack_edge_key(GridPos a, GridPos b);

class GridMap {
public:
    GridMap() = default;

    // Construct from an ASCII row list. Row 0 is the top (y=0).
    // Characters:
    //   '.' -> FLOOR
    //   'W' -> WALL
    //   'C' -> COVER
    // All rows must have equal length. Doors are added separately.
    static GridMap from_ascii(const std::vector<std::string>& rows);

    int width() const { return width_; }
    int height() const { return height_; }

    bool in_bounds(GridPos p) const {
        return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_;
    }

    CellType cell(GridPos p) const {
        return cells_[static_cast<size_t>(p.y) * width_ + p.x];
    }
    void set_cell(GridPos p, CellType t) {
        cells_[static_cast<size_t>(p.y) * width_ + p.x] = t;
    }

    // Passable: any non-WALL cell. Door states further restrict edge passability.
    bool passable(GridPos p) const {
        return in_bounds(p) && cell(p) != CellType::WALL;
    }

    // WALL blocks LOS. COVER does NOT block LOS (contract §3).
    bool blocks_los(GridPos p) const {
        return !in_bounds(p) || cell(p) == CellType::WALL;
    }

    // Door management. Adjacent-cell check is the caller's responsibility.
    size_t add_door(GridPos a, GridPos b, DoorState state);
    const std::vector<DoorEdge>& doors() const { return doors_; }
    std::vector<DoorEdge>& doors() { return doors_; }

    // Look up a door by its edge. Returns index into doors() or -1.
    int find_door(GridPos a, GridPos b) const;

    // Edge semantics between two adjacent cells (8-connected).
    // Returns true if a closed/locked door blocks this edge.
    bool edge_blocks_los(GridPos a, GridPos b) const;
    // Returns true if units can step from a to b given door state.
    bool edge_passable(GridPos a, GridPos b) const;

    // Bresenham line of sight from 'from' to 'to'. Checks cells along the
    // path for blocking and the door edge between any two consecutive cells.
    bool line_of_sight(GridPos from, GridPos to) const;

    // 8-connected neighbors of p that are passable and whose edge is passable.
    std::vector<GridPos> neighbors_8(GridPos p) const;

    // Room membership: -1 for WALL cells, else a non-negative room id.
    int room_of(GridPos p) const {
        if (!in_bounds(p)) return -1;
        return room_ids_[static_cast<size_t>(p.y) * width_ + p.x];
    }
    int room_count() const { return room_count_; }

    // Flood fill rooms. Must be called after the cell grid and doors are
    // fully populated. Rooms are bounded by WALL cells and
    // CLOSED/LOCKED door edges.
    void recompute_rooms();

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<CellType> cells_;
    std::vector<DoorEdge> doors_;
    std::unordered_map<uint64_t, size_t> door_index_;

    std::vector<int> room_ids_;
    int room_count_ = 0;
};
