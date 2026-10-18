#include "farming.h"
#include "cambc.h"
#include "../navigation/navigation.h"
#include <variant>

namespace Farming {
    bool retreat_and_heal() {
        auto me = cambc::self();
        if (me.health() < (me.max_health() * 0.35f) && !me.is_recalling()) {
            me.recall();
            return true;
        }
        return false;
    }

    bool execute_turn() {
        if (retreat_and_heal()) return true;

        auto me = cambc::self();

        // 1. CAPTURE CHECK
        auto visible_groups = cambc::vision::camp_groups();
        for (const auto& group : visible_groups) {
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(group.state())) {
                auto spawn_tiles = group.spawn_tiles().positions();
                if (spawn_tiles.empty()) continue;

                cambc::vec2 best_tile = spawn_tiles[0];
                int min_dist = Navigation::distance_squared(me.position(), best_tile);
                for (auto tile : spawn_tiles) {
                    int dist = Navigation::distance_squared(me.position(), tile);
                    if (dist < min_dist) {
                        min_dist = dist;
                        best_tile = tile;
                    }
                }

                if (min_dist == 0) return true; // Stand still to capture
                
                Navigation::step_towards(best_tile);
                return true;
            }
        }

        // 2. MONSTER FARMING
        auto camps = cambc::vision::camps();
        const cambc::camp* target_camp = nullptr;
        int min_dist = 999999;

        for (const auto& c : camps) {
            if (!c.is_alive()) continue;
            int dist = Navigation::distance_squared(me.position(), c.position());
            if (dist < min_dist) {
                min_dist = dist;
                target_camp = &c;
            }
        }

        if (target_camp) {
            if (min_dist <= me.attack_range() && me.can_attack()) {
                me.attack(*target_camp);
            } 
            if (me.can_move()) {
                Navigation::step_towards(target_camp->position());
            }
            return true;
        }

        // 3. EXPLORATION FALLBACK
        auto all_camp_groups = cambc::map::camp_groups();
        cambc::vec2 explore_target;
        int explore_dist = 999999;
        bool found_target = false;

        for (const auto& group : all_camp_groups) {
            auto spawn_tiles = group.spawn_tiles().positions();
            if (spawn_tiles.empty()) continue;

            cambc::vec2 tile = spawn_tiles[0];
            int dist = Navigation::distance_squared(me.position(), tile);
            
            if (dist > 2 && dist < explore_dist) {
                explore_dist = dist;
                explore_target = tile;
                found_target = true;
            }
        }

        if (found_target) {
            Navigation::step_towards(explore_target);
            return true;
        }

        return false;
    }
}