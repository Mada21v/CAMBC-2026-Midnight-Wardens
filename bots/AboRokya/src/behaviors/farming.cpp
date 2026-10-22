// behaviors/farming.cpp
#include "farming.h"
#include <cambc.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include "../navigation/navigation.h"
#include "combat.h"
#include "../core/memory.h"

namespace {

struct CampGroupInfo {
    cambc::map::camp_group group;
    cambc::bitboard spawns;
    int gem_value;
    int gold_value;
    bool is_monolith;
    size_t index;
};

// Persistent respawn round timer per camp group, shared across all champion turns
static std::vector<uint32_t> s_respawn_until;

const std::vector<CampGroupInfo>& get_all_camp_groups() {
    static const auto cached = [] {
        std::vector<CampGroupInfo> list;
        size_t idx = 0;
        for (const auto& g : cambc::map::camp_groups()) {
            bool mono = false;
            for (const auto& c : g.camps()) {
                if (c.is_monolith()) { mono = true; break; }
            }
            list.push_back({
                g,
                g.spawn_tiles(),
                static_cast<int>(g.gem_value()),
                static_cast<int>(g.gold_value()),
                mono,
                idx++
            });
        }
        s_respawn_until.assign(list.size(), 0);
        return list;
    }();
    return cached;
}

template<class Targets>
bool attack_first_in_range(const Targets& targets) {
    auto& me = cambc::self();
    if (targets.empty() || !me.can_attack()) return false;
    const auto attack_tiles = me.attack_tiles();
    for (const auto& target : targets) {
        if (target.is_visible() && attack_tiles.is_set(target.position()) && target.health() > 0) {
            // Never awaken dormant dangerous monsters (e.g. Monolith or 100+ strength) solo
            if (target.strength() >= 100.0f && !target.is_awake()) {
                continue;
            }
            try {
                me.attack(target);
                return true;
            } catch (...) {}
        }
    }
    return false;
}

void maybe_place_camp_ward() {
    auto& me = cambc::self();
    if (me.ward_cooldown() != 0) return;
    try {
        cambc::bitboard cands;
        cands.clear();
        cands.draw_chebyshev(me.position(), 2);
        cands &= ~Navigation::blocked_tiles();

        // Strategic Wards: Only place inside bushes (concealment vision), prioritizing altar bushes and enemy side
        cambc::bitboard bush_cands = cands & cambc::map::bushes();
        if (!bush_cands.is_empty()) {
            auto enemy_spawns = cambc::map::spawn_region(cambc::team::enemy).positions();
            cambc::vec2 enemy_spawn = enemy_spawns.empty() ? cambc::vec2{0, 0} : enemy_spawns[0];

            auto positions = bush_cands.positions();
            // Prioritize bushes near altars first, then bushes deeper towards enemy territory
            std::sort(positions.begin(), positions.end(), [&](cambc::vec2 a, cambc::vec2 b) {
                int altar_dist_a = 999, altar_dist_b = 999;
                for (auto altar : cambc::map::altars().positions()) {
                    altar_dist_a = std::min(altar_dist_a, static_cast<int>(a.chebyshev_distance(altar)));
                    altar_dist_b = std::min(altar_dist_b, static_cast<int>(b.chebyshev_distance(altar)));
                }
                if (altar_dist_a != altar_dist_b) {
                    return altar_dist_a < altar_dist_b;
                }
                return a.chebyshev_distance(enemy_spawn) < b.chebyshev_distance(enemy_spawn);
            });

            for (auto tile : positions) {
                try {
                    if (me.place_ward(tile)) return;
                } catch (...) {}
            }
        }
    } catch (...) {}
}

} // anonymous namespace

bool Farming::is_strong_carry(const cambc::champion& champ) {
    auto name = champ.champion_name();
    return (name == cambc::champion_type::varg || 
            name == cambc::champion_type::heart_of_ruin ||
            name == cambc::champion_type::vendetta ||
            name == cambc::champion_type::vesper_vale ||
            name == cambc::champion_type::ren ||
            name == cambc::champion_type::ryth ||
            name == cambc::champion_type::marcella);
}

bool Farming::can_clear_nearby_camp(const cambc::champion& me) {
    return me.health() >= me.max_health() * 0.35f;
}

int Farming::closest_active_camp_dist(const cambc::champion& me) {
    const auto& all_groups = get_all_camp_groups();
    uint32_t current_round = cambc::round_number();
    int min_dist = 999;
    for (const auto& cg : all_groups) {
        if (cg.gem_value == 0) continue; // Only consider gem-granting camps
        if (current_round < s_respawn_until[cg.index]) continue; // In respawn timer
        if (cg.is_monolith && me.max_health() < 1350.0f) continue;
        for (auto p : cg.spawns.positions()) {
            int d = static_cast<int>(me.position().chebyshev_distance(p));
            if (d < min_dist) min_dist = d;
        }
    }
    return min_dist;
}

void Farming::farm_optimal_camps() {
    auto& me = cambc::self();

    // 1. Attack enemy champions in range first
    if (Combat::try_attack_enemy_champions()) return;

    // 2. Attack camps in range
    if (attack_first_in_range(cambc::vision::camps())) return;

    const auto& all_groups = get_all_camp_groups();
    const auto visible = cambc::vision::visible_tiles();
    uint32_t current_round = cambc::round_number();

    // Update shared respawn tracking from visible camp groups
    for (const auto& cg : all_groups) {
        if (!(cg.spawns & visible).is_empty()) {
            try {
                auto state = cg.group.state();
                if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                    auto resp = std::get<cambc::map::camp_group_respawning>(state);
                    s_respawn_until[cg.index] = current_round + resp.timer;
                } else {
                    s_respawn_until[cg.index] = 0;
                }
            } catch (...) {}
        }
    }

    int my_slot = Memory::my_slot();

    // Check which camp groups are claimed by other allies in shared memory
    std::vector<bool> ally_claimed(all_groups.size(), false);
    for (int s = 0; s < 5; ++s) {
        if (s != my_slot) {
            int claimed = Memory::get_ally_claimed_camp(s);
            if (claimed >= 0 && claimed < static_cast<int>(all_groups.size())) {
                ally_claimed[claimed] = true;
            }
        }
    }

    // 3. Reward Capture Hold: If already standing on a camp spawn tile with reward ready, stay!
    for (const auto& cg : all_groups) {
        if (!(cg.spawns & visible).is_empty()) {
            try {
                if (std::holds_alternative<cambc::map::camp_group_reward_available>(cg.group.state())) {
                    if (cg.spawns.is_set(me.position())) {
                        maybe_place_camp_ward();
                        return;
                    }
                    // If an ally is ALREADY standing on this reward tile, do NOT crowd it!
                    bool ally_on_tile = false;
                    for (const auto& ally : cambc::vision::champions(cambc::team::ally)) {
                        if (ally != me && cg.spawns.is_set(ally.position())) {
                            ally_on_tile = true;
                            break;
                        }
                    }
                    if (ally_on_tile) {
                        ally_claimed[cg.index] = true;
                    }
                }
            } catch (...) {}
        }
    }

    if (!me.can_move()) return;

    // 3b. Visible Safe Camp Fast Approach:
    bool any_gem_camps_on_map = false;
    for (const auto& cg : all_groups) {
        if (cg.gem_value > 0) { any_gem_camps_on_map = true; break; }
    }

    cambc::bitboard visible_camp_goals;
    visible_camp_goals.clear();
    for (const auto& camp : cambc::vision::camps()) {
        try {
            if (!camp.is_visible() || camp.health() <= 0) continue;
            if (camp.strength() >= 100.0f && !camp.is_awake()) continue;
            // When gem camps exist, don't abandon gem objectives for 0-gem camps; on pure combat maps, farm gold!
            if (any_gem_camps_on_map) {
                try {
                    if (camp.is_gold_cluster() || (!camp.is_monolith() && camp.camp_group().gem_value() == 0)) continue;
                } catch (...) {}
            }
            auto approach = Navigation::tiles_around(camp.position(), me.attack_range());
            visible_camp_goals |= approach;
        } catch (...) {}
    }
    if (!visible_camp_goals.is_empty()) {
        if (Navigation::move_towards_closest(visible_camp_goals)) {
            if (!Combat::try_attack_enemy_champions()) {
                attack_first_in_range(cambc::vision::camps());
            }
            return;
        }
    }

    // 4. Build Multi-Camp Goal Bitboards:
    cambc::bitboard gem_goals;
    gem_goals.clear();
    cambc::bitboard gold_goals;
    gold_goals.clear();
    cambc::bitboard monolith_goals;
    monolith_goals.clear();

    for (const auto& cg : all_groups) {
        // Skip camps currently confirmed respawning
        if (current_round < s_respawn_until[cg.index]) {
            continue;
        }

        // Inter-Champion Partitioning:
        // Skip small camps (clusters or <= 15 gems) claimed by an ally
        if (ally_claimed[cg.index]) {
            if (cg.gem_value <= 15 || cg.group.camps().size() <= 2) {
                continue;
            }
        }

        if (cg.is_monolith) {
            monolith_goals |= cg.spawns;
        } else if (cg.gem_value > 0) {
            gem_goals |= cg.spawns;
        } else {
            gold_goals |= cg.spawns;
        }
    }

    // Fallback: If all camps are filtered out by ally claims, relax claim restrictions
    if (gem_goals.is_empty() && gold_goals.is_empty()) {
        for (const auto& cg : all_groups) {
            if (current_round < s_respawn_until[cg.index]) continue;
            if (cg.is_monolith) {
                monolith_goals |= cg.spawns;
            } else if (cg.gem_value > 0) {
                gem_goals |= cg.spawns;
            } else {
                gold_goals |= cg.spawns;
            }
        }
    }

    // 5. Goal Hierarchy:
    // - All champions strictly prioritize gem camps over 0-gem gold camps!
    // - Fall back to gold camps only when all gem camps are respawning
    // - Monolith maps: challenge Monolith if high HP (>= 1350) or endgame
    cambc::bitboard chosen_goals;
    if (!gem_goals.is_empty()) {
        chosen_goals = gem_goals;
    } else if (!gold_goals.is_empty()) {
        chosen_goals = gold_goals;
    } else if (!monolith_goals.is_empty()) {
        bool monolith_damaged = false;
        for (const auto& camp : cambc::vision::camps()) {
            if (camp.is_visible() && camp.is_monolith() && camp.health() < 8000.0f) {
                monolith_damaged = true;
                break;
            }
        }
        if (me.max_health() >= 1350.0f || current_round >= 800 || monolith_damaged) {
            chosen_goals = monolith_goals;
        }
    }

    if (!chosen_goals.is_empty()) {
        if (Navigation::move_towards_closest(chosen_goals)) {
            // Claim the camp group closest to our current position that matches our target goals
            int best_cg_idx = -1;
            int best_dist = 999;
            for (const auto& cg : all_groups) {
                if (current_round < s_respawn_until[cg.index]) continue;
                if (ally_claimed[cg.index]) continue;
                if ((chosen_goals & cg.spawns).is_empty()) continue;
                for (auto p : cg.spawns.positions()) {
                    int d = static_cast<int>(me.position().chebyshev_distance(p));
                    if (d < best_dist) {
                        best_dist = d;
                        best_cg_idx = static_cast<int>(cg.index);
                    }
                }
            }
            if (best_cg_idx >= 0) {
                Memory::claim_camp_group(best_cg_idx);
            }

            // Fresh vision after moving: attack if now in range!
            if (!Combat::try_attack_enemy_champions()) {
                attack_first_in_range(cambc::vision::camps());
            }
        } else {
            // Terrain obstacle fallback: If walking couldn't path to goals, check if teleport crystal can reach across!
            try {
                if (me.consumables()[cambc::consumable_type::tp_crystal].count() > 0) {
                    cambc::bitboard tc_range;
                    tc_range.clear();
                    tc_range.draw_chebyshev(me.position(), 3);
                    tc_range &= ~Navigation::blocked_tiles();
                    tc_range &= cambc::vision::visible_tiles();

                    // Find a reachable tile that is closer to chosen goals
                    for (auto tile : tc_range.positions()) {
                        for (auto goal : chosen_goals.positions()) {
                            if (tile.chebyshev_distance(goal) < me.position().chebyshev_distance(goal)) {
                                if (me.consumables()[cambc::consumable_type::tp_crystal].use(tile)) {
                                    return;
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
        }
    }
}
