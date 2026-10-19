// navigation/pathfinding.cpp
#include "navigation.h"

namespace Navigation {

    // 1. Static terrain that can NEVER be crossed on foot
    cambc::bitboard terrain_walls() {
        return cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
    }

    // 2. Public accessor (declared in navigation.h)
    cambc::bitboard blocked_tiles() {
        return terrain_walls();
    }

    // 3. Dynamic obstacles to avoid stepping on THIS turn
    cambc::bitboard immediate_obstacles() {
        auto blocked = terrain_walls() | cambc::vision::champion_tiles() | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        // Never consider our own position blocked
        blocked.set(cambc::self().position(), false);
        return blocked;
    }

    bool move_towards_closest(cambc::bitboard goals) {
        auto& me = cambc::self();
        if (!me.can_move()) return false;
        if (goals.is_set(me.position())) return false;

        const auto walls = terrain_walls();

        // Compute BFS distance field from the GOALS as roots.
        cambc::distance_field field;
        cambc::bitboard visited;
        field.compute(walls & ~goals, goals, visited);

        // If our tile was never reached, the goals are completely walled off
        if (!visited.is_set(me.position())) return false;

        const auto dynamic_blocked = immediate_obstacles();

        // 1. Primary: next_move_from returns the DIRECTION DELTA (e.g. {0, 1}, {1, 0})
        auto next_step_delta = field.next_move_from(me.position());
        if (next_step_delta.has_value()) {
            cambc::vec2 target_tile = me.position() + *next_step_delta;
            if (target_tile != me.position() && !dynamic_blocked.is_set(target_tile)) {
                return me.move_absolute(target_tile);
            }
        }

        // 2. Fallback: Scan all 8 adjacent tiles.
        // If the direct tile is blocked by an ally or monster, take an unblocked
        // alternative that improves (or sideways-maintains) distance to the goal.
        int current_dist = field.get(me.position());
        cambc::vec2 best = me.position();
        int best_dist = current_dist;
        bool found_strictly_closer = false;

        static const cambc::vec2 dirs[] = {
            {0, 1}, {1, 1}, {1, 0}, {1, -1},
            {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}
        };

        for (const auto& dir : dirs) {
            cambc::vec2 candidate = me.position() + dir;
            if (cambc::map::is_in_bounds(candidate) && visited.is_set(candidate) && !dynamic_blocked.is_set(candidate)) {
                int d = field.get(candidate);
                if (d < best_dist) {
                    best_dist = d;
                    best = candidate;
                    found_strictly_closer = true;
                } else if (!found_strictly_closer && d == current_dist && best == me.position()) {
                    best = candidate;
                }
            }
        }

        if (best != me.position()) {
            return me.move_absolute(best);
        }

        return false;
    }

} // namespace Navigation