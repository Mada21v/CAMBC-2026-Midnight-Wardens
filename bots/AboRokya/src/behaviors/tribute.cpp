#include "tribute.h"
#include <cambc.h>
#include <algorithm>
#include "../navigation/navigation.h"
#include "combat.h"
#include "../core/memory.h"

bool Tribute::maybe_place_altar_lock() {
    auto& me = cambc::self();
    try {
        if (me.consumables()[cambc::consumable_type::altar_lock].count() > 0) {
            auto enemy_spawns = cambc::map::spawn_region(cambc::team::enemy).positions();
            cambc::vec2 enemy_spawn = enemy_spawns.empty() ? cambc::vec2{0, 0} : enemy_spawns[0];

            auto altars = cambc::map::altars().positions();
            // Prioritize locking the enemy's altar first!
            std::sort(altars.begin(), altars.end(), [&](cambc::vec2 a, cambc::vec2 b) {
                return a.chebyshev_distance(enemy_spawn) < b.chebyshev_distance(enemy_spawn);
            });

            for (auto altar : altars) {
                if (me.position().chebyshev_distance(altar) <= 2) {
                    if (!cambc::vision::has_altar_lock(altar)) {
                        if (me.consumables()[cambc::consumable_type::altar_lock].use(altar)) {
                            return true;
                        }
                    }
                }
            }
        }
    } catch (...) {}
    return false;
}

bool Tribute::try_tribute() {
    auto& me = cambc::self();

    // Broadcast tributing state and release any camp claim
    Memory::set_tributing(true);
    Memory::clear_claimed_camp();

    // 1. If an enemy altar lock is already in attack range, strike it!
    if (Combat::try_attack_altar_lock()) {
        return true;
    }

    // 2. Win-condition / Fast Banking:
    // If ALREADY within submit range of an open altar, start submitting immediately!
    for (auto altar : cambc::map::altars().positions()) {
        if (me.position().chebyshev_distance(altar) <= cambc::constants::submit_gems_range) {
            bool is_enemy_lock = false;
            if (cambc::vision::has_altar_lock(altar)) {
                try {
                    if (cambc::vision::altar_lock_at(altar).is_enemy()) {
                        is_enemy_lock = true;
                    }
                } catch (...) {}
            }
            if (!is_enemy_lock) {
                try {
                    me.submit_gems(altar);
                    return true;
                } catch (...) {}
            }
        }
    }

    // 3. Place our altar lock if within 2 tiles and altar is open
    maybe_place_altar_lock();

    // 4. Build approach goals to all friendly/unlocked altars
    cambc::bitboard goals;
    goals.clear();
    cambc::bitboard locked_altar_goals;
    locked_altar_goals.clear();
    
    for (auto altar : cambc::map::altars().positions()) {
        bool has_lock = cambc::vision::has_altar_lock(altar);
        bool locked_by_enemy = false;
        if (has_lock) {
            try {
                auto lock = cambc::vision::altar_lock_at(altar);
                if (lock.is_enemy()) {
                    locked_by_enemy = true;
                }
            } catch (...) {}
        }

        cambc::bitboard altar_range;
        altar_range.clear();
        altar_range.draw_chebyshev(altar, cambc::constants::submit_gems_range);

        if (!locked_by_enemy) {
            goals |= altar_range;
        } else {
            locked_altar_goals |= altar_range;
        }
    }
    
    goals &= ~cambc::map::altars();
    locked_altar_goals &= ~cambc::map::altars();

    cambc::bitboard target_goals = (!goals.is_empty()) ? goals : locked_altar_goals;

    if (!target_goals.is_empty()) {
        bool moved = Navigation::move_towards_closest(target_goals);

        // After moving, if we are now in submit range of an altar, submit immediately!
        for (auto altar : cambc::map::altars().positions()) {
            if (me.position().chebyshev_distance(altar) <= cambc::constants::submit_gems_range) {
                bool is_enemy_lock = false;
                if (cambc::vision::has_altar_lock(altar)) {
                    try {
                        auto lock = cambc::vision::altar_lock_at(altar);
                        if (lock.is_enemy()) {
                            is_enemy_lock = true;
                            if (me.attack_tiles().is_set(altar)) {
                                if (me.can_attack()) {
                                    try { me.attack(lock); } catch (...) {}
                                }
                                return true; // Engaging enemy lock: do NOT walk away!
                            }
                        }
                    } catch (...) {}
                }

                if (!is_enemy_lock) {
                    try {
                        me.submit_gems(altar);
                        return true;
                    } catch (...) {}
                }

                // If not submitting, try placing an altar lock
                maybe_place_altar_lock();
                return true; // Already at altar: hold position!
            }
        }

        if (moved) return true;
        if (target_goals.is_set(me.position())) return true; // Already in target area
    }

    return false;
}
