#include "shop.h"
#include <cambc.h>
#include <vector>

bool Shop::buy_optimal_shards() {
    auto& me = cambc::self();
    if (me.gold_count() < 10) return false;

    auto type = me.champion_name();
    bool purchased_ever = false;

    // 1. Champion-specific optimal build orders
    std::vector<cambc::item_type> build_order;

    if (type == cambc::champion_type::varg) {
        // Varg: Frontline splash tank & primary jungle farmer
        // Bruiser emblem gives 5 Str, 1 Focus (bigger shield), 1 HP for immediate early survivability!
        // Monster shard increases splash damage to clear camps in seconds!
        // Jungle shard gives -30% camp damage taken so Varg NEVER dies to monsters!
        build_order = {
            cambc::item_type::bruiser_shard_of_strength,  // Str + Focus (strengthens shield) + HP
            cambc::item_type::monster_shard_of_strength,  // +10% camp damage + basic attack splash
            cambc::item_type::jungle_shard_of_health,      // -30% camp damage taken + HP (camp survival!)
            cambc::item_type::durability_shard_of_health, // -10% damage taken from all sources
            cambc::item_type::power_shard_of_strength
        };
    } else if (type == cambc::champion_type::vesper_vale) {
        // Vesper Vale: 55 base Strength, ranged basic attacker, +33% base gold passive!
        // Stacking Treasure Emblem (+20% gold) gives +53% total bonus gold!
        build_order = {
            cambc::item_type::treasure_shard_of_strength, // +20% bonus gold + Strength!
            cambc::item_type::monster_shard_of_strength,  // Camp damage + Strength!
            cambc::item_type::power_shard_of_strength,    // Raw physical scaling on 3-range attack
            cambc::item_type::bruiser_shard_of_strength   // Str + Focus + HP
        };
    } else if (type == cambc::champion_type::vendetta) {
        // Vendetta: Assassin, kill resets ALL cooldowns!
        // Needs high physical burst to trigger instant resets and fast camp executes
        build_order = {
            cambc::item_type::monster_shard_of_strength,  // Fast camp execution
            cambc::item_type::power_shard_of_strength,    // Raw physical burst for resets
            cambc::item_type::bruiser_shard_of_strength,  // Str + Focus + HP
            cambc::item_type::durability_shard_of_health  // Survivability
        };
    } else if (type == cambc::champion_type::ryth) {
        // Ryth: Passive converts 1% of bonus HP directly into bonus Strength!
        // Durability + Power Health gives massive HP pool and huge bonus attack power
        build_order = {
            cambc::item_type::durability_shard_of_health, // HP emblem + -10% damage taken + bonus Str!
            cambc::item_type::power_shard_of_health,      // Extra HP converts to Str
            cambc::item_type::bruiser_shard_of_health,    // HP + Str + Focus
            cambc::item_type::monster_shard_of_strength
        };
    } else if (type == cambc::champion_type::marcella) {
        // Marcella: Stun-immune frontline melee & gem submitter (+2 gold per gem tributed)
        build_order = {
            cambc::item_type::durability_shard_of_health, // Frontline tankiness (-10% damage taken)
            cambc::item_type::monster_shard_of_strength,  // Camp damage
            cambc::item_type::bruiser_shard_of_strength,  // Str + HP
            cambc::item_type::power_shard_of_strength
        };
    } else if (type == cambc::champion_type::stellar || type == cambc::champion_type::cassi) {
        // Pure Focus Casters
        build_order = {
            cambc::item_type::power_shard_of_focus,       // Power Emblem: 6 focus shards
            cambc::item_type::monster_shard_of_focus,     // Monster Emblem: +10% camp damage
            cambc::item_type::durability_shard_of_focus,  // Durability Emblem: -10% damage taken
            cambc::item_type::bruiser_shard_of_focus      // Bruiser Emblem: Focus + Str + HP
        };
    } else {
        // Bruiser / Fighter Frontliners (Heart of Ruin, Ren)
        build_order = {
            cambc::item_type::bruiser_shard_of_strength,  // Bruiser Emblem: Str + Focus + HP
            cambc::item_type::monster_shard_of_strength,  // Monster Emblem: +10% camp damage
            cambc::item_type::durability_shard_of_health, // Durability Emblem: -10% damage taken
            cambc::item_type::power_shard_of_strength     // Power Emblem: raw physical damage
        };
    }

    // 1. Altar Lock Priority: Ensure champion carries 1 Altar Lock to secure contested altars
    if (me.gold_count() >= 10) {
        try {
            if (me.can_buy(cambc::consumable_type::altar_lock) && 
                me.consumables()[cambc::consumable_type::altar_lock].count() == 0) {
                me.buy(cambc::consumable_type::altar_lock);
                purchased_ever = true;
            }
        } catch (...) {}
    }

    // 2. Main Stat Shards: Invest gold into core champion build order
    bool bought_any = true;
    while (bought_any && me.gold_count() >= 30) {
        bought_any = false;
        for (const auto& item : build_order) {
            if (me.can_buy(item, 1)) {
                try {
                    me.buy(item, 1);
                    bought_any = true;
                    purchased_ever = true;
                    break; // Restart from highest priority with updated gold count
                } catch (...) {
                    // Try next item if capped
                }
            }
        }
    }

    // 3. Excess Gold: Teleport Crystal for mobility / obstacle jumping
    if (me.gold_count() >= 10) {
        try {
            if (me.can_buy(cambc::consumable_type::tp_crystal) && 
                me.consumables()[cambc::consumable_type::tp_crystal].count() == 0) {
                me.buy(cambc::consumable_type::tp_crystal);
                purchased_ever = true;
            }
        } catch (...) {}
    }

    return purchased_ever;
}
