#include "tribute.h"
#include <cambc.h>
#include "../navigation/navigation.h"

bool Tribute::try_tribute() {
    auto& me = cambc::self();
    cambc::bitboard goals;
    goals.clear();
    
    for (auto altar : cambc::map::altars().positions()) {
        // Tributing requires standing within Chebyshev distance 1[cite: 1]
        if (me.position().chebyshev_distance(altar) <= 1 && !cambc::vision::has_altar_lock(altar)) {
            me.submit_gems(altar);
            return true;
        }
        // If not in range, add the altar's perimeter to our pathfinding goals
        if (!cambc::vision::has_altar_lock(altar)) {
            cambc::bitboard altar_range;
            altar_range.clear();
            altar_range.draw_chebyshev(altar, 1);
            goals |= altar_range;
        }
    }
    
    Navigation::move_towards_closest(goals);
    return true;
}