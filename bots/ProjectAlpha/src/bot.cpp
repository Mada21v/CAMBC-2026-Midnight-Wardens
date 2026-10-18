// Champion decisions. main.cpp dispatches here after the draft.
#include <cambc.h>
#include "bot.h"
#include <array>
#include <iostream>
#include "random.h"
#include <cstdint>
#include <span>
#include <variant>
#include <utility>
#include <vector>

using cambc::vec2;
using cambc::team;

int distance(vec2 a, vec2 b) {
    return static_cast<int>(a.chebyshev_distance(b));
}

cambc::bitboard tiles_around(vec2 position, int radius) {
    cambc::bitboard tiles;
    tiles.clear();
    tiles.draw_chebyshev(position, radius);
    return tiles;
}

// Combine terrain and visible occupants using native bitboard operations.
cambc::bitboard blocked_tiles() {
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(team::enemy) | cambc::vision::champion_tiles()
        | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    blocked.set(cambc::self().position(), false);
    return blocked;
}

bool move_towards_closest(cambc::bitboard goals) {
    auto& me = cambc::self();
    if (!me.can_move() || goals.is_set(me.position()))
        return false;

    // Only free goals or occupied camp tiles can seed the field.
    const auto blocked = blocked_tiles();
    goals &= ~blocked | cambc::vision::camp_tiles();
    if (goals.is_empty())
        return false;
    if (goals.count() == 1 && !blocked.is_set(goals.positions()[0]))
        return me.move_towards(goals.positions()[0]);

    // Expand from the goals until our current tile is reached.
    const auto path_walls = blocked & ~goals;
    cambc::distance_field field;
    cambc::bitboard visited;
    field.compute_to(path_walls, goals, tiles_around(me.position(), 0), visited);

    // Choose a reachable step with a smaller distance to the goal.
    auto step = me.position();
    for (auto tile : blocked.reachable_tiles(step, me.base_stats().base_move_speed).positions())
        if (field.get(tile) < field.get(step)) step = tile;

    return step != me.position() && me.move_absolute(step);
}

void Bot::buy_items() {
    auto& me = cambc::self();
    const std::array items{cambc::item_type::power_shard_of_strength,
        cambc::item_type::power_shard_of_focus, cambc::item_type::power_shard_of_health};
    const auto item = items[chosen_stat];
    const unsigned count = me.gold_count() / item.cost();
    if (count && me.can_buy(item, count)) me.buy(item, count);
}

// Compute the attack mask once, rather than once per visible target.
template<class Targets>
bool attack_first_in_range(const Targets& targets) {
    auto& me = cambc::self();
    if (targets.empty() || !me.can_attack()) return false;
    const auto attack_tiles = me.attack_tiles();
    for (const auto& target : targets) {
        if (target.is_visible() && attack_tiles.is_set(target.position()) && target.health() > 0) {
            me.attack(target);
            return true;
        }
    }
    return false;
}

// Attack a camp already in range, otherwise move toward the nearest camp tiles.
void farm_camps() {
    auto& me = cambc::self();
    auto visible_camps = cambc::vision::camps();
    auto allies = cambc::vision::champions(team::ally);

    // ---------------------------------------------------------
    // 1. UNIFIED TARGET SELECTION (Anchor to Home Base)
    // ---------------------------------------------------------
    cambc::vec2 my_base = cambc::map::spawn_region(team::ally).positions()[0];
    
    cambc::vec2 best_cluster(0,0), best_camp(0,0);
    int dist_cluster = 99999, dist_camp = 99999;
    bool has_cluster = false, has_camp = false;

    // Priority 1: The single Gem Cluster closest to our BASE
    for (const auto& c : visible_camps) {
        if (c.max_health() == 500) { // Identifies Gem/Gold clusters[cite: 1]
            int d = distance(c.position(), my_base);
            if (d < dist_cluster) { dist_cluster = d; best_cluster = c.position(); has_cluster = true; }
        }
    }

    // Priority 2: The single Active Camp closest to our BASE
    static const auto camp_groups = [] {
        std::vector<std::pair<cambc::map::camp_group, cambc::bitboard>> groups;
        for (const auto& group : cambc::map::camp_groups()) groups.emplace_back(group, group.spawn_tiles());
        return groups;
    }();
    const auto visible = cambc::vision::visible_tiles();

    for (const auto& [group, spawns] : camp_groups) {
        bool is_dead = !(spawns & visible).is_empty() && std::holds_alternative<cambc::map::camp_group_respawning>(group.state());
        if (!is_dead) {
            int d = distance(spawns.positions()[0], my_base);
            if (d < dist_camp) { dist_camp = d; best_camp = spawns.positions()[0]; has_camp = true; }
        }
    }

    // Assign the universal target that EVERY champion will share
    cambc::vec2 target_center = my_base; 
    if (has_cluster) target_center = best_cluster;
    else if (has_camp) target_center = best_camp;
    else {
        if (attack_first_in_range(visible_camps)) return;
        return; // Fallback if map is empty
    }

    // ---------------------------------------------------------
    // 2. SQUAD COUNTING
    // ---------------------------------------------------------
    int combat_ready_allies = 0;
    for (const auto& a : allies) {
        bool is_retreating = a.is_recalling() || a.is_submitting_gems() || 
                             (a.gem_count() > 0 && a.health() < a.max_health() * 0.50f);
        if (a.health() > 0 && !is_retreating) {
            if (distance(me.position(), a.position()) <= 5) combat_ready_allies++;
        }
    }

    // ---------------------------------------------------------
    // 3. THE RALLY POINT (Group up BEFORE engaging)
    // ---------------------------------------------------------
    bool near_target = (distance(me.position(), target_center) <= 5);

    if (near_target && combat_ready_allies < 2) {
        // We are at the camp, but our backup isn't here yet.
        // Calculate the SAFE RALLY TILE (Distance 5 from camp, facing our base)
        cambc::bitboard rally_candidates = tiles_around(target_center, 5) & ~blocked_tiles();
        cambc::vec2 best_rally = target_center;
        int min_b_dist = 99999;
        
        for (auto p : rally_candidates.positions()) {
            int d = distance(p, my_base);
            if (d < min_b_dist) { min_b_dist = d; best_rally = p; }
        }

        // DO NOT ATTACK YET. Path perfectly to the Rally Tile to form a tight group.
        cambc::bitboard rally_goal; rally_goal.clear();
        rally_goal.set(best_rally, true);
        move_towards_closest(rally_goal);
        return; 
    }

    // ---------------------------------------------------------
    // 4. EXECUTE (Squad is formed!)
    // ---------------------------------------------------------
    cambc::bitboard attack_goals; attack_goals.clear();
    attack_goals.set(target_center, true);

    if (attack_first_in_range(visible_camps)) return;
    move_towards_closest(attack_goals);
}

void update_radar(cambc::vec2 enemy_pos) {
    TeamMemory state{0};
    cambc::shared_memory.read(0, std::span{reinterpret_cast<uint8_t*>(&state.raw), 8});
    
    state.radar.enemy_x = enemy_pos.x;
    state.radar.enemy_y = enemy_pos.y;
    state.radar.round_spotted = cambc::round_number();
    state.radar.is_valid = 1;

    cambc::shared_memory.write(0, std::span{reinterpret_cast<const uint8_t*>(&state.raw), 8});
}

void Bot::play_turn() {
    if (chosen_stat < 0) chosen_stat = random_number(3);

    auto& me = cambc::self();
    if (!me.is_alive() || me.is_recalling() || me.is_submitting_gems() || me.is_teleporting() 
        || (!me.can_move() && !me.can_attack())) {
        return;
    }

    auto enemies = cambc::vision::champions(team::enemy);
    auto allies = cambc::vision::champions(team::ally);
    bool in_real_fight = !enemies.empty();
    
    int combat_ready_allies = 0;
    for (const auto& a : allies) {
        bool is_retreating = a.is_recalling() || a.is_submitting_gems() || 
                             (a.gem_count() > 0 && a.health() < a.max_health() * 0.50f);
        if (a.health() > 0 && !is_retreating) {
            if (distance(me.position(), a.position()) <= 5) {
                combat_ready_allies++;
            }
        }
    }

    // ---------------------------------------------------------
    // BASE HEALING LOGIC
    // ---------------------------------------------------------
    cambc::bitboard my_spawn = cambc::map::spawn_region(team::ally);
    bool on_spawn = my_spawn.is_set(me.position());

    if (on_spawn) {
        buy_items(); 
        if (me.health() < me.max_health() * 0.95f) return; // Stay to heal
    }

    // ---------------------------------------------------------
    // CLEAN RECALL LOGIC (No hallucinated vision wipes)
    // ---------------------------------------------------------
    bool needs_healing = (me.health() < me.max_health() * 0.25f) && !in_real_fight;
    bool massive_wealth = (me.gold_count() >= 150) && !in_real_fight;
    // Only run away if we are hurt AND we physically have zero backup locally
    bool isolated_and_hurt = (me.health() < me.max_health() * 0.60f) && (combat_ready_allies == 0) && !in_real_fight;

    if (!on_spawn && (needs_healing || massive_wealth || isolated_and_hurt)) {
        if (me.gem_count() == 0) {
            me.recall();
            return;
        }
    }

    // ---------------------------------------------------------
    // CONDITIONAL ALTAR TRIBUTE 
    // ---------------------------------------------------------
    if (me.gem_count() > 0 && (combat_ready_allies < 2 || me.health() < me.max_health() * 0.50f)) {
        cambc::vec2 my_base = my_spawn.positions()[0];
        cambc::vec2 best_altar;
        int best_score = 99999;

        for (auto altar : cambc::map::altars().positions()) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range && !cambc::vision::has_altar_lock(altar)) {
                me.submit_gems(altar); 
                return;
            }
            if (!cambc::vision::has_altar_lock(altar)) {
                int score = distance(me.position(), altar) + (distance(altar, my_base) * 3);
                if (score < best_score) {
                    best_score = score; best_altar = altar;
                }
            }
        }
        move_towards_closest(tiles_around(best_altar, cambc::constants::submit_gems_range)); 
        return;
    }

    // ---------------------------------------------------------
    if (cambc::round_number() < 200) {
        cambc::bitboard enemy_spawn = cambc::map::spawn_region(team::enemy);
        cambc::vec2 enemy_base = enemy_spawn.is_empty() ? cambc::vec2(50,50) : enemy_spawn.positions()[0];
        
        cambc::vec2 target_camp;
        int min_dist = 9999;
        for (const auto& group : cambc::map::camp_groups()) {
            cambc::vec2 camp_pos = group.spawn_tiles().positions()[0];
            int d = distance(camp_pos, enemy_base);
            if (d < min_dist) { min_dist = d; target_camp = camp_pos; }
        }

        bool enemy_is_low = false;
        for (const auto& e : enemies) {
            if (e.health() < e.max_health() * 0.50f) enemy_is_low = true;
        }
        
        bool numbers_advantage = (allies.size() + 1) > enemies.size();
        bool in_bush = cambc::map::bushes().is_set(me.position());
        
        // ONLY count as hiding if we are actually in a bush right next to the enemy camp
        bool correctly_hidden = in_bush && (distance(me.position(), target_camp) <= 6);

        if (!enemies.empty()) {
            if (numbers_advantage || enemy_is_low || !correctly_hidden) {
                if (attack_first_in_range(enemies)) return;
                cambc::bitboard chase; chase.clear();
                for (const auto& e : enemies) chase.set(e.position(), true);
                if (move_towards_closest(chase)) return;
            } else {
                return; 
            }
        }

        // We only trigger the trap once we reach the 6-tile radius of the target camp
        if (distance(me.position(), target_camp) <= 6) {
            if (!correctly_hidden) {
                cambc::bitboard bushes = cambc::map::bushes();
                cambc::bitboard ambush_goals = bushes & tiles_around(target_camp, 6);
                if (!ambush_goals.is_empty() && move_towards_closest(ambush_goals)) return;
            } else {
                return; // We are perfectly hidden in the correct ambush bush. Wait.
            }
        } else {
            if (move_towards_closest(tiles_around(target_camp, 3))) return;
        }
    }

    farm_camps();
    me.set_priority(1 + random_number(100));
}