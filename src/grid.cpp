#include "grid.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

const char* cell_type_str(CellType t) {
    switch (t) {
        case CellType::FLOOR: return "FLOOR";
        case CellType::WALL:  return "WALL";
        case CellType::COVER: return "COVER";
    }
    return "???";
}

const char* door_state_str(DoorState s) {
    switch (s) {
        case DoorState::OPEN:   return "OPEN";
        case DoorState::CLOSED: return "CLOSED";
        case DoorState::LOCKED: return "LOCKED";
    }
    return "???";
}

// Canonical 64-bit key: smaller packed (x,y) in the high 32 bits.
uint64_t pack_edge_key(GridPos a, GridPos b) {
    auto pack = [](GridPos p) -> uint32_t {
        return (static_cast<uint32_t>(static_cast<uint16_t>(p.x)) << 16) |
                static_cast<uint32_t>(static_cast<uint16_t>(p.y));
    };
    uint32_t ua = pack(a);
    uint32_t ub = pack(b);
    if (ua > ub) std::swap(ua, ub);
    return (static_cast<uint64_t>(ua) << 32) | static_cast<uint64_t>(ub);
}

GridMap GridMap::from_ascii(const std::vector<std::string>& rows) {
    GridMap g;
    if (rows.empty())
        throw std::runtime_error("GridMap::from_ascii: empty rows");
    g.height_ = static_cast<int>(rows.size());
    g.width_ = static_cast<int>(rows[0].size());
    if (g.width_ <= 0)
        throw std::runtime_error("GridMap::from_ascii: empty row");
    g.cells_.assign(static_cast<size_t>(g.width_) * g.height_, CellType::FLOOR);
    g.room_ids_.assign(g.cells_.size(), -1);

    for (int y = 0; y < g.height_; ++y) {
        const std::string& r = rows[y];
        if (static_cast<int>(r.size()) != g.width_)
            throw std::runtime_error("GridMap::from_ascii: ragged row at y=" + std::to_string(y));
        for (int x = 0; x < g.width_; ++x) {
            CellType t;
            switch (r[x]) {
                case '.': t = CellType::FLOOR; break;
                case 'W': t = CellType::WALL; break;
                case 'C': t = CellType::COVER; break;
                default:
                    throw std::runtime_error(
                        std::string("GridMap::from_ascii: unknown cell char '") + r[x] + "'");
            }
            g.cells_[static_cast<size_t>(y) * g.width_ + x] = t;
        }
    }
    return g;
}

size_t GridMap::add_door(GridPos a, GridPos b, DoorState state) {
    // Enforce adjacency (8-connected).
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    if ((dx > 1 || dy > 1) || (dx == 0 && dy == 0))
        throw std::runtime_error("GridMap::add_door: door cells must be 8-adjacent");

    DoorEdge e{a, b, state};
    uint64_t key = pack_edge_key(a, b);
    auto it = door_index_.find(key);
    if (it != door_index_.end()) {
        doors_[it->second] = e;
        return it->second;
    }
    size_t idx = doors_.size();
    doors_.push_back(e);
    door_index_[key] = idx;
    return idx;
}

int GridMap::find_door(GridPos a, GridPos b) const {
    auto it = door_index_.find(pack_edge_key(a, b));
    if (it == door_index_.end()) return -1;
    return static_cast<int>(it->second);
}

bool GridMap::edge_blocks_los(GridPos a, GridPos b) const {
    int idx = find_door(a, b);
    if (idx < 0) return false;
    DoorState s = doors_[static_cast<size_t>(idx)].state;
    return s != DoorState::OPEN;
}

bool GridMap::edge_passable(GridPos a, GridPos b) const {
    if (!passable(a) || !passable(b)) return false;
    int idx = find_door(a, b);
    if (idx < 0) return true;
    return doors_[static_cast<size_t>(idx)].state == DoorState::OPEN;
}

bool GridMap::line_of_sight(GridPos from, GridPos to) const {
    if (!in_bounds(from) || !in_bounds(to)) return false;
    if (from == to) return true;

    // Canonicalize endpoints so LOS is symmetric. Bresenham step order
    // depends on direction of traversal; with a fixed direction the same
    // pair always walks the same cells.
    if (to.y < from.y || (to.y == from.y && to.x < from.x))
        std::swap(from, to);

    // Bresenham, integer-only. We iterate stepping one axis at a time so each
    // step moves by at most one cell, which lets us check the door edge
    // between consecutive cells. For diagonal steps we need to check both
    // intermediate edges (no cutting a corner through a door).
    int x0 = from.x, y0 = from.y;
    int x1 = to.x,   y1 = to.y;
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    GridPos prev{static_cast<int16_t>(x0), static_cast<int16_t>(y0)};
    while (x0 != x1 || y0 != y1) {
        int e2 = 2 * err;
        int nx = x0, ny = y0;
        bool diag = false;
        if (e2 > -dy && e2 < dx) {
            // Diagonal
            nx = x0 + sx;
            ny = y0 + sy;
            err -= dy;
            err += dx;
            diag = true;
        } else if (e2 > -dy) {
            nx = x0 + sx;
            err -= dy;
        } else if (e2 < dx) {
            ny = y0 + sy;
            err += dx;
        }
        GridPos next{static_cast<int16_t>(nx), static_cast<int16_t>(ny)};

        // Cell at 'next' must not block LOS (unless it is the target).
        if (!(next == to) && blocks_los(next)) return false;

        if (diag) {
            // Diagonal move: check both orthogonal intermediate edges so a
            // diagonal LOS cannot slip past a closed door on either side.
            GridPos mid_h{static_cast<int16_t>(x0 + sx), static_cast<int16_t>(y0)};
            GridPos mid_v{static_cast<int16_t>(x0), static_cast<int16_t>(y0 + sy)};
            if (edge_blocks_los(prev, mid_h) && edge_blocks_los(prev, mid_v))
                return false;
            if (edge_blocks_los(mid_h, next) && edge_blocks_los(mid_v, next))
                return false;
        } else {
            if (edge_blocks_los(prev, next)) return false;
        }

        x0 = nx;
        y0 = ny;
        prev = next;
    }
    return true;
}

std::vector<GridPos> GridMap::neighbors_8(GridPos p) const {
    std::vector<GridPos> out;
    out.reserve(8);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            GridPos q{static_cast<int16_t>(p.x + dx), static_cast<int16_t>(p.y + dy)};
            if (!in_bounds(q)) continue;
            if (!edge_passable(p, q)) continue;
            out.push_back(q);
        }
    }
    return out;
}

void GridMap::recompute_rooms() {
    room_ids_.assign(cells_.size(), -1);
    room_count_ = 0;

    const auto idx = [&](GridPos p) {
        return static_cast<size_t>(p.y) * width_ + p.x;
    };

    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            GridPos start{static_cast<int16_t>(x), static_cast<int16_t>(y)};
            if (cell(start) == CellType::WALL) continue;
            if (room_ids_[idx(start)] >= 0) continue;

            int room_id = room_count_++;
            std::queue<GridPos> q;
            q.push(start);
            room_ids_[idx(start)] = room_id;
            while (!q.empty()) {
                GridPos cur = q.front(); q.pop();
                // 4-connected for room membership. Diagonal movement should
                // not bridge two rooms that are separated by orthogonal walls.
                static const int dxs[4] = { 1, -1,  0,  0 };
                static const int dys[4] = { 0,  0,  1, -1 };
                for (int k = 0; k < 4; ++k) {
                    GridPos nb{static_cast<int16_t>(cur.x + dxs[k]),
                               static_cast<int16_t>(cur.y + dys[k])};
                    if (!in_bounds(nb)) continue;
                    if (cell(nb) == CellType::WALL) continue;
                    if (room_ids_[idx(nb)] >= 0) continue;

                    // A CLOSED or LOCKED door separates rooms; an OPEN door
                    // (or no door) does not.
                    int d = find_door(cur, nb);
                    if (d >= 0 && doors_[static_cast<size_t>(d)].state != DoorState::OPEN)
                        continue;

                    room_ids_[idx(nb)] = room_id;
                    q.push(nb);
                }
            }
        }
    }
}
