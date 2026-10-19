// behaviors/farming.cpp
#include "farming.h"
#include <cambc.h>
#include "../navigation/navigation.h"

using cambc::vec2;

// ============================================================
//  Helper utilities (file-local)
// ============================================================
namespace {

    // Early game threshold — champions should group for the first ~150 rounds
    bool is_early_game() {
        return cambc::round_number() < 150;
    }

    // Check whether any visible allied champion is within Chebyshev `range`.
    bool has_nearby_ally(int range) {
        auto& me = cambc::self();
        for (const auto& champ : cambc::vision::champions()) {
            if (champ.team() == cambc::team::ally && champ.position() != me.position()) {
                if (me.position().chebyshev_distance(champ.position()) <= range) {
                    return true;
                }
            }
        }
        return false;
    }

    // Safely check if a camp group is respawning without throwing api_not_in_vision
    bool is_group_respawning_safely(const cambc::map::camp_group& group, const cambc::bitboard& visible) {
        // Can only query state() if at least one member spawn tile is currently visible!
        bool any_spawn_visible = false;
        for (const auto& member : group.camps()) {
            if (visible.is_set(member.spawn_position())) {
                any_spawn_visible = true;
                break;
            }
        }
        if (!any_spawn_visible) return false;

        try {
            return std::holds_alternative<cambc::map::camp_group_respawning>(group.state());
        } catch (...) {
            return false;
        }
    }

} // anonymous namespace


// ============================================================
//  Main farming entry point
// ============================================================
void Farming::farm_optimal_camps() {
    auto& me = cambc::self();
    const auto visible = cambc::vision::visible_tiles();

    // ---------------------------------------------------------
    //  Step 1: Check if any camp monster is alive and visible
    // ---------------------------------------------------------
    const cambc::unit* best_target = nullptr;
    float best_hp = 1e9f;
    auto attack_tiles = me.attack_tiles();

    for (const auto& target : cambc::vision::camps()) {
        if (target.is_visible() && target.is_alive() && target.health() > 0) {
            if (attack_tiles.is_set(target.position())) {
                // Focus the lowest health monster to secure kill-resets (Vendetta) quickly
                if (target.health() < best_hp) {
                    best_hp = target.health();
                    best_target = &target;
                }
            }
        }
    }

    // 1a. Melee can move AND attack on the same turn!
    // 1b. Ranged CANNOT move and basic attack on the same turn.
    //     So if ranged can attack, attack immediately without moving.
    //     If melee can move, we first move closer or move then attack.
    if (best_target != nullptr) {
        auto champ_name = me.champion_name();
        bool is_ranged = (champ_name == cambc::champion_type::cassi || 
                          champ_name == cambc::champion_type::stellar ||
                          champ_name == cambc::champion_type::vesper_vale ||
                          champ_name == cambc::champion_type::prism ||
                          champ_name == cambc::champion_type::tavi ||
                          champ_name == cambc::champion_type::willow);
        if (is_ranged) {
            if (me.can_attack()) {
                me.attack(*best_target);
                return;
            }
        } else {
            // Melee: attack if in range
            if (me.can_attack()) {
                me.attack(*best_target);
                // After attacking, melee can still move towards camp capture tiles or the next monster!
            }
        }
    }

    if (!me.can_move()) return;

    // ---------------------------------------------------------
    //  Step 2: Build distance field from personal position
    // ---------------------------------------------------------
    cambc::bitboard terrain = cambc::map::walls() | cambc::map::holes() |
                               cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);

    cambc::distance_field dist_field;
    cambc::bitboard visited;
    cambc::bitboard self_origin;
    self_origin.clear();
    self_origin.set(me.position());
    dist_field.compute(terrain, self_origin, visited);

    bool early = is_early_game();

    // ---------------------------------------------------------
    //  Step 3 (early-game): Solo champion -> prioritize free clusters
    // ---------------------------------------------------------
    if (early && !has_nearby_ally(10)) {
        cambc::bitboard best_free_goals;
        best_free_goals.clear();
        float best_free_score = -1.0f;

        for (const auto& group : cambc::map::camp_groups()) {
            if (is_group_respawning_safely(group, visible)) continue;

            bool is_cluster_only = true;
            float cluster_score = 0;
            for (const auto& camp : group.camps()) {
                if (camp.kind() == cambc::camp_type::gem_cluster) {
                    cluster_score += 10000;
                } else if (camp.kind() == cambc::camp_type::gold_cluster) {
                    cluster_score += 500;
                } else {
                    is_cluster_only = false;
                    break;
                }
            }

            if (!is_cluster_only || cluster_score <= 0) continue;

            auto spawns = group.spawn_tiles();
            vec2 group_center = spawns.positions()[0];
            int dist = dist_field.get(group_center);
            if (dist == 0 && group_center != me.position()) continue;
            if (dist == cambc::distance_field::unreachable) continue;
            if (dist == 0) dist = 1;

            float score = cluster_score / static_cast<float>(dist);
            if (score > best_free_score) {
                best_free_score = score;
                best_free_goals = spawns;
            } else if (score == best_free_score) {
                best_free_goals |= spawns;
            }
        }

        if (!best_free_goals.is_empty()) {
            Navigation::move_towards_closest(best_free_goals);
            return;
        }
    }

    // ---------------------------------------------------------
    //  Step 4: Standard / Grouped camp scoring
    // ---------------------------------------------------------
    cambc::distance_field shared_spawn_field;
    cambc::bitboard spawn_visited;
    if (early) {
        cambc::bitboard spawn_roots = cambc::map::spawn_region(cambc::team::ally);
        shared_spawn_field.compute(terrain, spawn_roots, spawn_visited);
    }

    cambc::bitboard best_goals;
    best_goals.clear();
    float best_score = -1.0f;

    for (const auto& group : cambc::map::camp_groups()) {
        if (is_group_respawning_safely(group, visible)) continue;

        float raw_score = 0;
        for (const auto& camp : group.camps()) {
            if      (camp.kind() == cambc::camp_type::gem_cluster)  raw_score += 10000;
            else if (camp.kind() == cambc::camp_type::elk)          raw_score += 2000;
            else if (camp.kind() == cambc::camp_type::wolf)         raw_score += 1000;
            else if (camp.kind() == cambc::camp_type::gold_cluster) raw_score += 500;
        }

        auto spawns = group.spawn_tiles();
        vec2 group_center = spawns.positions()[0];

        int my_dist = dist_field.get(group_center);
        if (my_dist == cambc::distance_field::unreachable) continue;
        if (my_dist == 0 && group_center != me.position()) continue;

        int effective_dist = my_dist;
        if (early) {
            int spawn_dist = shared_spawn_field.get(group_center);
            if (spawn_dist != 0 && spawn_dist != cambc::distance_field::unreachable) {
                effective_dist = spawn_dist;
            }
        }
        if (effective_dist == 0) effective_dist = 1;

        float final_score = raw_score / static_cast<float>(effective_dist);

        if (final_score > best_score) {
            best_score = final_score;
            best_goals = spawns;
        } else if (final_score == best_score) {
            best_goals |= spawns;
        }
    }

    if (!best_goals.is_empty()) {
        Navigation::move_towards_closest(best_goals);
    }
}