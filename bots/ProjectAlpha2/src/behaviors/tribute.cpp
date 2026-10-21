#include "tribute.h"
#include <cambc.h>
#include "../navigation/navigation.h"
#include "combat.h"
#include "../core/memory.h"

bool Tribute::maybe_place_altar_lock() {
    auto& me = cambc::self();
    try {
        if (me.consumables()[cambc::consumable_type::altar_lock].count() > 0) {
            for (auto altar : cambc::map::altars().positions()) {
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

    // 2. Try placing our altar lock if adjacent to an unlocked altar
    maybe_place_altar_lock();

    // 3. If ALREADY within submit range of an open altar, start submitting now!
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
                maybe_place_altar_lock();

                bool is_enemy_lock = false;
                if (cambc::vision::has_altar_lock(altar)) {
                    try {
                        auto lock = cambc::vision::altar_lock_at(altar);
                        if (lock.is_enemy()) {
                            is_enemy_lock = true;
                            if (me.attack_tiles().is_set(altar)) {
                                try {
                                    me.attack(lock);
                                    return true;
                                } catch (...) {}
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
            }
        }

        if (moved) return true;
    }

    return false;
}
