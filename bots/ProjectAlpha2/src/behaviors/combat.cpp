#include "combat.h"
#include "cambc.h"
#include "../navigation/navigation.h"

namespace Combat {
    bool execute_turn() {
        auto me = cambc::self();
        auto enemies = cambc::vision::champions(cambc::team::enemy);
        if (enemies.empty()) return false;

        // Find closest living enemy
        const cambc::champion* target = nullptr;
        int min_dist = 999999;

        for (const auto& enemy : enemies) {
            if (!enemy.is_alive()) continue;
            int dist = Navigation::distance_squared(me.position(), enemy.position());
            if (dist < min_dist) {
                min_dist = dist;
                target = &enemy;
            }
        }

        if (!target) return false;

        // A. WEAVE ABILITIES (Spells / Resets / Dashes)
        if (me.can_cast()) {
            for (const auto& spell : me.abilities()) {
                if (!spell.is_ready()) continue;

                // 1. Attack Reset Abilities (cast on self to reset attack cooldown)
                if (spell.is_attack_reset()) {
                    if (min_dist <= me.attack_range()) {
                        spell.use(); // Resets attack cooldown instantly
                    }
                }
                // 2. Targeted Damage / Stun Spells
                else if (spell.is_damage() || spell.is_cc() || spell.is_aoe_damage()) {
                    if (min_dist <= 4) { // Standard spell range
                        if (spell.can_target_enemies()) {
                            spell.use(*target);
                            return true;
                        } else if (spell.can_target_ground()) {
                            spell.use(target->position());
                            return true;
                        }
                    }
                }
                // 3. Mobility / Gap-closing Dashes
                else if (spell.is_dash() && min_dist > me.attack_range() && min_dist <= 4) {
                    spell.use(target->position());
                    return true;
                }
            }
        }

        // B. BASIC ATTACK
        if (min_dist <= me.attack_range() && me.can_attack()) {
            me.attack(*target);
            return true;
        }

        // C. CHASE
        if (me.can_move()) {
            Navigation::step_towards(target->position());
            return true;
        }

        return false;
    }
}