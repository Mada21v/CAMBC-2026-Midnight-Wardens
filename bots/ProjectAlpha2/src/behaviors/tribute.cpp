#include "tribute.h"
#include "cambc.h"
#include "../navigation/navigation.h"

namespace Tribute {
    bool attempt_tribute() {
        auto me = cambc::self();
        
        // The dispatcher already checks this, but it's good practice to verify
        if (me.gem_count() == 0) return false;

        // Fetch all altar positions on the map
        auto altars = cambc::map::altars().positions(); 
        if (altars.empty()) return false;

        cambc::vec2 best_altar;
        int min_dist = 999999;

        // Find the closest altar
        for (auto altar : altars) {
            int dist = Navigation::distance_squared(me.position(), altar);
            if (dist < min_dist) {
                min_dist = dist;
                best_altar = altar;
            }
        }

        // You must be within Chebyshev distance 1 of an altar to tribute
        if (min_dist <= 1) {
            me.submit_gems(best_altar);
            return true;
        } 
        // If we aren't close enough, walk towards it
        else if (me.can_move()) {
            Navigation::step_towards(best_altar);
            return true;
        }

        return false;
    }
}