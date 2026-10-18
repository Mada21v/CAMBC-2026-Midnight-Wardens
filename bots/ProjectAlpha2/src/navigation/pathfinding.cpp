#include "navigation.h"
#include "cambc.h"

namespace Navigation {
    int distance_squared(cambc::vec2 a, cambc::vec2 b) {
        return a.chebyshev_distance(b);
    }

    bool step_towards(cambc::vec2 target) {
        auto me = cambc::self();
        if (!me.can_move()) return false;

        // 1. Try the engine's built-in pathfinder
        if (me.move_towards(target)) return true;

        // 2. FALLBACK: The engine's move_towards() fails if the EXACT target tile is occupied 
        // We just need to get in attack range, so we step into the closest free adjacent tile manually.
        cambc::vec2 dirs[8] = {
            cambc::vec2::north, cambc::vec2::north_east, cambc::vec2::east, cambc::vec2::south_east,
            cambc::vec2::south, cambc::vec2::south_west, cambc::vec2::west, cambc::vec2::north_west
        };

        // Sort the 8 directions by distance to our target
        for (int i = 0; i < 7; ++i) {
            for (int j = 0; j < 7 - i; ++j) {
                int distA = (me.position() + dirs[j]).chebyshev_distance(target);
                int distB = (me.position() + dirs[j+1]).chebyshev_distance(target);
                if (distA > distB) {
                    cambc::vec2 temp = dirs[j];
                    dirs[j] = dirs[j+1];
                    dirs[j+1] = temp;
                }
            }
        }

        // Attempt to move in the best available direction that isn't blocked
        for (auto dir : dirs) {
            if (me.move(dir)) return true;
        }
        
        return false;
    }
}