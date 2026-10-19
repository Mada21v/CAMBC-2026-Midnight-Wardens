#include "shop.h"
#include <cambc.h>

void Shop::buy_optimal_shards() {
    auto& me = cambc::self();
    cambc::item_type item = cambc::item_type::bruiser_shard_of_strength; 

    // Cassi & Stellar scale massively with Focus[cite: 1]
    auto type = me.champion_name();
    if (type == cambc::champion_type::cassi || type == cambc::champion_type::stellar) {
        item = cambc::item_type::power_shard_of_focus;
    } 
    // Varg, Ren, Vendetta, Issac scale best with Bruiser Strength/Health[cite: 1]

    // Buy as many 30-gold shards as possible[cite: 1]
    unsigned count = me.gold_count() / 30; 
    if (count > 0 && me.can_buy(item, count)) {
        me.buy(item, count);
    }
}