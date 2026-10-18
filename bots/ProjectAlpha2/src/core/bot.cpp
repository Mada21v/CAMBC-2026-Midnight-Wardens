#include "bot.h"
#include <cambc.h>
#include "../behaviors/combat.h"
#include "../behaviors/farming.h"
#include "../behaviors/shop.h"
#include "../behaviors/tribute.h"

namespace Bot {
    void auto_place_ward() {
        auto me = cambc::self();
        if (me.ward_cooldown() == 0) {
            cambc::vec2 target = me.position() + cambc::vec2::north * 2;
            if (cambc::map::is_in_bounds(target)) {
                me.place_ward(target);
            }
        }
    }

    void play_turn() {
        auto me = cambc::self();

        // Skip actions during channel states (DO NOT call end_turn())
        if (me.is_recalling() || me.is_submitting_gems()) {
            return;
        }

        Shop::evaluate_and_buy();
        auto_place_ward();

        bool acted = false;
        if (me.gem_count() > 0) {
            acted = Tribute::attempt_tribute();
        }

        if (!acted) {
            acted = Combat::execute_turn();
        }

        if (!acted) {
            Farming::execute_turn();
        }

        // main.cpp handles yielding the turn!
    }
}