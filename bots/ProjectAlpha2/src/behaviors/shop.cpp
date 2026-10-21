#include "shop.h"
#include <cambc.h>
#include <vector>

bool Shop::buy_optimal_shards() {
    auto& me = cambc::self();
    if (me.gold_count() < 10) return false;

    auto type = me.champion_name();
    bool purchased_ever = false;

    // 1. Buy Altar Lock consumable if affordable and not currently carrying one
    try {
        if (me.can_buy(cambc::consumable_type::altar_lock) && 
            me.consumables()[cambc::consumable_type::altar_lock].count() == 0 &&
            me.gold_count() >= 10) {
            me.buy(cambc::consumable_type::altar_lock);
            purchased_ever = true;
        }
    } catch (...) {}

    if (me.gold_count() < 30) return purchased_ever;

    // 2. Champion-specific optimal build orders
    std::vector<cambc::item_type> build_order;

    if (type == cambc::champion_type::vesper_vale) {
        // Vesper Vale: 55 base Strength, ranged basic attacker, +33% gold passive!
        // Stacking Treasure Emblem (+20% gold) gives +53% total bonus gold!
        build_order = {
            cambc::item_type::treasure_shard_of_strength, // +20% bonus gold + Strength!
            cambc::item_type::monster_shard_of_strength,  // +10% camp damage + Strength!
            cambc::item_type::bruiser_shard_of_strength,  // Strength + Focus + HP!
            cambc::item_type::power_shard_of_strength     // 6th Strength shard value!
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
        // Bruiser / Fighter Frontliners (Varg, Vendetta, Ren, Heart of Ruin, Marcella)
        build_order = {
            cambc::item_type::bruiser_shard_of_strength,  // Bruiser Emblem: Str + Focus + HP
            cambc::item_type::monster_shard_of_strength,  // Monster Emblem: +10% camp damage
            cambc::item_type::durability_shard_of_health, // Durability Emblem: -10% damage taken
            cambc::item_type::power_shard_of_strength,    // Power Emblem: high physical damage
            cambc::item_type::jungle_shard_of_health      // Jungle Emblem: -30% camp damage taken
        };
    }

    // Keep buying shards while we have gold and an item can be bought
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
                    // If purchase fails (e.g. inventory slot capped), try next item
                }
            }
        }
    }
    return purchased_ever;
}
