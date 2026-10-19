#include "bot.h"
#include <cambc.h>
#include "../behaviors/shop.h"
#include "../behaviors/tribute.h"
#include "../behaviors/farming.h"
#include "../behaviors/combat.h"

void Bot::play_turn() {
    auto& me = cambc::self();

    // 1. Safety & Validity: Skip turns if dead, channelling, or unable to act
    if (!me.is_alive() || me.is_recalling() || me.is_submitting_gems() || 
        me.is_teleporting() || (!me.can_move() && !me.can_attack() && !me.can_cast())) {
        return;
    }

    bool at_spawn = cambc::map::spawn_region(cambc::team::ally).is_set(me.position());

    // 2. Economy: Auto-buy the best shards when on the spawn tile
    if (at_spawn) {
        Shop::buy_optimal_shards();
    }

    // 3. Survival & Economy: Recall if badly hurt OR if holding enough gold for a full Emblem (150g)
    // Only recall if we don't hold valuable gems that need tributing first
    if (!at_spawn) {
        if (me.health() < me.max_health() * 0.30f || (me.gold_count() >= 150 && me.gem_count() < 15)) {
            me.recall();
            return;
        }
    }

    // 4. Objective Play: If we hold 15+ gems, secure them at an altar
    if (me.gem_count() >= 15) {
        if (Tribute::try_tribute()) {
            // Also cast abilities if enemies are nearby while moving/tributing
            Combat::try_combat_abilities();
            return;
        }
    }

    // 5. Cast combat abilities (shields, attack resets, damage spells)
    Combat::try_combat_abilities();

    // 6. Macro Farming & Movement
    Farming::farm_optimal_camps();

    // 7. Cast attack reset abilities again if attack was used during farming
    Combat::try_combat_abilities();
}