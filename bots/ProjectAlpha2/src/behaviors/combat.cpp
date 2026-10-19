#include "combat.h"
#include <cambc.h>

namespace Combat {

void try_combat_abilities() {
    auto& me = cambc::self();
    if (!me.can_cast()) return;

    for (const auto& ab : me.abilities()) {
        if (!ab.is_ready()) continue;

        // 1. Attack Reset (Issac Slot 0, Varg Slot 0, Vendetta Slot 0)
        // Walking compatible, resets attack allowance after attacking
        if (ab.is_attack_reset()) {
            if (!me.can_attack()) {
                ab.use();
                return;
            }
        }

        // 2. Self Shield (Varg Slot 1)
        // Walking compatible
        if (ab.is_shield()) {
            ab.use();
            return;
        }

        // 3. Spells with cast targets (CC / damage)
        // If an ability excludes walking and we already moved or need to move,
        // casting it after walking throws engine error 126.
        // Therefore: ONLY cast spells that exclude walking if we CANNOT move
        // or have not moved yet and are already in range!
        bool is_attack_spell = ab.is_damage() || ab.is_aoe_damage() || ab.is_cc();
        if (is_attack_spell) {
            // Check visible enemy champions first
            for (const auto& enemy : cambc::vision::champions()) {
                if (enemy.team() == cambc::team::enemy && enemy.is_alive() && enemy.is_visible()) {
                    if (me.position().chebyshev_distance(enemy.position()) <= 4) {
                        try {
                            if (ab.is_aoe_damage()) {
                                if (ab.use(enemy.position())) return;
                            } else {
                                if (ab.use(enemy)) return;
                            }
                        } catch (...) {
                            // If engine rejects due to movement exclusion, continue gracefully
                        }
                    }
                }
            }

            // Check visible neutral monsters
            for (const auto& camp : cambc::vision::camps()) {
                if (camp.is_visible() && camp.is_alive() && camp.health() > 0) {
                    if (me.position().chebyshev_distance(camp.position()) <= 4) {
                        try {
                            if (ab.is_aoe_damage()) {
                                if (ab.use(camp.position())) return;
                            } else {
                                if (ab.use(camp)) return;
                            }
                        } catch (...) {
                            // If engine rejects due to movement exclusion, continue gracefully
                        }
                    }
                }
            }
        }
    }
}

} // namespace Combat
