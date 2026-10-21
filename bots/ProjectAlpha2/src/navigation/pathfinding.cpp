// navigation/pathfinding.cpp
#include "navigation.h"

namespace Navigation {

    cambc::bitboard terrain_walls() {
        return cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
    }

    cambc::bitboard blocked_tiles() {
        auto blocked = terrain_walls() | cambc::vision::champion_tiles() | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        blocked.set(cambc::self().position(), false);
        return blocked;
    }

    cambc::bitboard tiles_around(cambc::vec2 position, int radius) {
        cambc::bitboard tiles;
        tiles.clear();
        tiles.draw_chebyshev(position, radius);
        return tiles;
    }

    bool try_traversal_dash(const cambc::distance_field& field) {
        auto& me = cambc::self();
        if (!me.can_cast()) return false;

        // In combat-heavy / small brawl maps (e.g. collision, contest), save dashes exclusively for combat!
        if (std::max(cambc::map::width(), cambc::map::height()) <= 28) return false;

        // Save dash for combat if any enemy champions are visible
        if (!cambc::vision::champions(cambc::team::enemy).empty()) return false;

        // Only use traversal dash if there is a long distance to target (at least 6 tiles away)
        int current_dist = field.get(me.position());
        if (current_dist < 6) return false;

        for (const auto& ab : me.abilities()) {
            if (!ab.is_ready() || !ab.is_dash()) continue;

            cambc::bitboard cands;
            try {
                cands = ab.valid_tiles();
            } catch (...) {
                cands.clear();
                cands.draw_chebyshev(me.position(), 2);
            }
            cands &= ~blocked_tiles() & cambc::vision::visible_tiles();

            cambc::vec2 best_tile = me.position();
            int best_dist = current_dist;

            for (auto tile : cands.positions()) {
                int d = field.get(tile);
                if (d < best_dist) {
                    best_dist = d;
                    best_tile = tile;
                }
            }

            if (best_dist < current_dist && best_tile != me.position()) {
                try {
                    if (ab.use(best_tile)) return true;
                } catch (...) {}
            }
        }
        return false;
    }

    bool move_towards_closest(cambc::bitboard goals) {
        auto& me = cambc::self();
        if (goals.is_set(me.position()))
            return false;

        const auto blocked = blocked_tiles();
        goals &= ~blocked | cambc::vision::camp_tiles();
        if (goals.is_empty())
            return false;

        const auto path_walls = blocked & ~goals;
        cambc::distance_field field;
        cambc::bitboard visited;
        field.compute_to(path_walls, goals, tiles_around(me.position(), 0), visited);

        if (!visited.is_set(me.position())) return false;

        auto step = me.position();
        int best_dist = field.get(step);
        auto reachable = blocked.reachable_tiles(step, me.base_stats().base_move_speed);

        for (auto tile : reachable.positions()) {
            if (visited.is_set(tile) && field.get(tile) < best_dist) {
                best_dist = field.get(tile);
                step = tile;
            }
        }

        if (step != me.position() && me.can_move()) {
            try {
                if (me.move_absolute(step)) {
                    // After successful move, try traversal dash if goal is still far away
                    try_traversal_dash(field);
                    return true;
                }
            } catch (...) {}
        }

        // Lateral step fallback if blocked by an ally
        if (me.can_move()) {
            for (auto tile : reachable.positions()) {
                if (tile != me.position() && visited.is_set(tile) && field.get(tile) == field.get(me.position())) {
                    try {
                        if (me.move_absolute(tile)) {
                            try_traversal_dash(field);
                            return true;
                        }
                    } catch (...) {}
                }
            }
        }

        // If regular move couldn't be made, attempt traversal dash
        if (try_traversal_dash(field)) return true;

        return false;
    }

    bool move_away_from(cambc::vec2 danger_pos) {
        auto& me = cambc::self();
        if (!me.can_move()) return false;

        const auto blocked = blocked_tiles();
        auto reachable = blocked.reachable_tiles(me.position(), 1);

        int current_dist = me.position().chebyshev_distance(danger_pos);
        int best_dist = current_dist;
        cambc::vec2 best_tile = me.position();

        cambc::vec2 spawn_center = cambc::map::spawn_region(cambc::team::ally).positions()[0];
        int best_spawn_dist = 999999;

        for (const auto& cand : reachable.positions()) {
            if (cand == me.position()) continue;
            int dist = cand.chebyshev_distance(danger_pos);
            if (dist > best_dist) {
                best_dist = dist;
                best_tile = cand;
                best_spawn_dist = cand.chebyshev_distance(spawn_center);
            } else if (dist == best_dist && dist > current_dist) {
                int s_dist = cand.chebyshev_distance(spawn_center);
                if (s_dist < best_spawn_dist) {
                    best_spawn_dist = s_dist;
                    best_tile = cand;
                }
            }
        }

        if (best_tile != me.position()) {
            try {
                return me.move_absolute(best_tile);
            } catch (...) {
                return false;
            }
        }
        return false;
    }

} // namespace Navigation
