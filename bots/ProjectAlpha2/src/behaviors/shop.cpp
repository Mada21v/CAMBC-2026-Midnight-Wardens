#include "shop.h"
#include "cambc.h"

namespace Shop {
    void evaluate_and_buy() {
        auto me = cambc::self();

        // Must be standing on an allied spawn tile (or dead) to shop
        if (!cambc::map::spawn_region(me.team()).is_set(me.position())) {
            // Remote shop exception for Tavi
            if (me.champion_name() != cambc::champion_type::tavi) {
                return;
            }
        }

        // Buy Altar Lock consumable if on spawn and affordably priced (10g)
        if (me.gold_count() >= 10 && me.consumables()[cambc::consumable_type::altar_lock].count() == 0) {
            if (me.can_buy(cambc::consumable_type::altar_lock)) {
                me.buy(cambc::consumable_type::altar_lock);
            }
        }

        // Select optimal shard stat based on champion kit
        cambc::item_type primary_shard = cambc::item_type::monster_shard_of_strength;

        switch (me.champion_name()) {
            case cambc::champion_type::cassi:
            case cambc::champion_type::prism:
            case cambc::champion_type::stellar:
            case cambc::champion_type::willow:
                primary_shard = cambc::item_type::power_shard_of_focus;
                break;

            case cambc::champion_type::ryth:
            case cambc::champion_type::marcella:
            case cambc::champion_type::ren:
                primary_shard = cambc::item_type::durability_shard_of_health;
                break;

            case cambc::champion_type::vesper_vale:
            case cambc::champion_type::vendetta:
            case cambc::champion_type::issac:
            case cambc::champion_type::varg:
            case cambc::champion_type::tavi:
            default:
                primary_shard = cambc::item_type::monster_shard_of_strength;
                break;
        }

        // Purchase shards (5 matching shards automatically merge into Emblems)
        while (me.gold_count() >= 30) {
            if (me.can_buy(primary_shard)) {
                me.buy(primary_shard);
            } else {
                break;
            }
        }
    }
}