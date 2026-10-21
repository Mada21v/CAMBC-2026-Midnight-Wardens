#include "bot.h"
#include <cambc.h>
#include <algorithm>
#include "../behaviors/shop.h"
#include "../behaviors/tribute.h"
#include "../behaviors/farming.h"
#include "../behaviors/combat.h"
#include "../navigation/navigation.h"
#include "memory.h"

namespace {

bool should_tribute_now(const cambc::champion& me) {
    uint32_t gems = me.gem_count();
    if (gems == 0) return false;

    // 1. Win-condition sprint: bank to reach or exceed 1000 gems!
    uint32_t submitted = cambc::global_submitted_gems(cambc::team::ally);
    if (submitted + gems >= 1000) return true;
    if (submitted >= 900 && gems >= 10) return true;

    // Helper: is an altar open or locked by our team (not locked by enemy)?
    auto is_altar_open_for_us = [](cambc::vec2 altar) {
        if (!cambc::vision::has_altar_lock(altar)) return true;
        try {
            return !cambc::vision::altar_lock_at(altar).is_enemy();
        } catch (...) {
            return true;
        }
    };

    // 2. In submit range of an open or friendly-locked altar -> bank immediately!
    for (auto altar : cambc::map::altars().positions()) {
        if (me.position().chebyshev_distance(altar) <= cambc::constants::submit_gems_range) {
            if (is_altar_open_for_us(altar)) return true;
        }
    }

    // 3. Right next to an altar (<= 3 tiles away) with carried gems (>= 5): bank quickly!
    if (gems >= 5) {
        for (auto altar : cambc::map::altars().positions()) {
            if (me.position().chebyshev_distance(altar) <= 3) {
                if (is_altar_open_for_us(altar)) return true;
            }
        }
    }

    // 4. Dynamic endgame thresholds:
    uint32_t round = cambc::round_number();
    if (round >= 1800) return true;               // Final 200 rounds: bank ANY gems!
    if (round >= 1600 && gems >= 5) return true;   // Late game: bank >= 5 gems!
    if (round >= 1400 && gems >= 15) return true;  // Mid-late game: bank >= 15 gems!

    // 5. Low HP (< 45%) -> bank gems to avoid dropping them on death!
    if (me.health() < me.max_health() * 0.45f && gems >= 5) return true;

    // 6. Carrying Threshold reached:
    // Strong carries (Varg, Ren, Vendetta, Heart of Ruin) bank at 35+ gems
    // Normal champions bank at 15+ gems
    bool is_strong = Farming::is_strong_carry(me);
    uint32_t threshold = is_strong ? 35 : 15;
    if (gems >= threshold) return true;

    // 7. "Nothing else to do" rule:
    // If carrying gems (>= 5) and the nearest active gem camp is far away (> 14 tiles),
    // or all active gem camps are dead/respawning, or nearest altar is closer than any active camp:
    if (gems >= 5) {
        int camp_dist = Farming::closest_active_camp_dist(me);
        if (camp_dist == 999) return true; // No camps available anywhere!

        int altar_dist = 999;
        for (auto altar : cambc::map::altars().positions()) {
            if (is_altar_open_for_us(altar)) {
                int d = static_cast<int>(me.position().chebyshev_distance(altar));
                if (d < altar_dist) altar_dist = d;
            }
        }

        if (camp_dist > 14 || (gems >= 15 && altar_dist <= camp_dist)) {
            return true;
        }
    }

    return false;
}

} // anonymous namespace

void Bot::play_turn() {
    try {
        auto& me = cambc::self();

        // 1. Safety: Skip turns if dead, channelling, or unable to act
        if (!me.is_alive() || me.is_recalling() || me.is_submitting_gems() || 
            me.is_teleporting() || (!me.can_move() && !me.can_attack() && !me.can_cast())) {
            Memory::clear_claimed_camp();
            return;
        }

        // Dynamic role priority to prevent bottlenecks and pathing blockages
        me.set_priority(Memory::get_recommended_priority());

        const auto spawn_tiles = cambc::map::spawn_region(cambc::team::ally);
        int dist_to_spawn = 999;
        for (auto p : spawn_tiles.positions()) {
            dist_to_spawn = std::min(dist_to_spawn, static_cast<int>(me.position().chebyshev_distance(p)));
        }
        bool at_spawn = (dist_to_spawn == 0);

        // 2. Economy: Auto-buy shards and consumables on spawn
        if (at_spawn) {
            Shop::buy_optimal_shards();
        }

        // 3. Hospital Hold: If on spawn and injured (< 75% HP), stay on spawn to regenerate!
        // Prevents champions from stepping 1 tile off spawn and getting stuck in a recall loop
        if (at_spawn && me.health() < me.max_health() * 0.75f) {
            Memory::clear_claimed_camp();
            Combat::try_combat_abilities();
            Combat::try_attack_enemy_champions();
            return;
        }

        // 4. Place Altar Lock if passing near an open altar
        Tribute::maybe_place_altar_lock();

        // 5. Objective Banking: Submit gems when threshold reached, low HP, or in altar range
        if (should_tribute_now(me)) {
            if (Tribute::try_tribute()) {
                return;
            }
        } else {
            Memory::set_tributing(false);
        }

        // 6. Survival & Disengage (Health < 35%):
        if (!at_spawn && me.health() < me.max_health() * 0.35f) {
            bool near_altar = false;
            for (auto a : cambc::map::altars().positions()) {
                if (me.position().chebyshev_distance(a) <= cambc::constants::submit_gems_range) {
                    near_altar = true;
                    break;
                }
            }
            if (!near_altar || me.gem_count() == 0) {
                // If close to spawn (<= 5 tiles), walk straight into spawn instead of wasting 5 rounds channeling recall!
                if (dist_to_spawn <= 5) {
                    Memory::clear_claimed_camp();
                    Combat::try_combat_abilities();
                    Combat::try_attack_enemy_champions();
                    Navigation::move_towards_closest(spawn_tiles);
                    return;
                }

                bool threat_near = false;
                for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
                    if (enemy.is_visible() && enemy.is_alive() && enemy.health() > 0) {
                        if (me.position().chebyshev_distance(enemy.position()) <= 4) {
                            threat_near = true;
                            break;
                        }
                    }
                }
                if (!threat_near) {
                    for (const auto& camp : cambc::vision::camps()) {
                        if (camp.is_visible() && camp.is_alive() && camp.health() > 0 && camp.is_awake()) {
                            if (me.position().chebyshev_distance(camp.position()) <= 3) {
                                threat_near = true;
                                break;
                            }
                        }
                    }
                }

                if (threat_near) {
                    // Defensive shield / support spells
                    Combat::try_combat_abilities();

                    // Strike back if an enemy is in direct attack range
                    Combat::try_attack_enemy_champions();

                    // Fall back towards ally spawn region
                    Memory::clear_claimed_camp();
                    Navigation::move_towards_closest(spawn_tiles);
                    return;
                } else {
                    // Safe distance from threats (> 4 tiles) AND far from spawn (> 5 tiles): recall!
                    Memory::clear_claimed_camp();
                    try { me.recall(); return; } catch (...) {}
                }
            }
        }

        // 7. Coordinated Combat: Attack enemy champions in range first
        if (Combat::try_attack_enemy_champions()) {
            Combat::try_combat_abilities();
        }

        // 8. Tactical Chase: If healthy and enemies are nearby, close distance to focus fire
        if (Combat::try_chase_visible_enemy()) {
            return;
        }

        // 9. Attack neutral monster in range
        if (me.can_attack()) {
            for (const auto& camp : cambc::vision::camps()) {
                if (camp.is_visible() && me.attack_tiles().is_set(camp.position()) && camp.health() > 0) {
                    if (camp.strength() >= 100.0f && !camp.is_awake()) continue;
                    try {
                        me.attack(camp);
                        break;
                    } catch (...) {}
                }
            }
        }

        // 10. Macro Movement & Camp Farming
        Farming::farm_optimal_camps();

        // 11. Cast combat abilities AFTER movement
        Combat::try_combat_abilities();
    } catch (...) {}
}
