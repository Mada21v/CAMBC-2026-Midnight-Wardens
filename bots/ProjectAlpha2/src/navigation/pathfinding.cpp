#include "navigation.h"
#include <algorithm>
#include <array>

namespace Navigation {

    int distance_squared(cambc::vec2 a, cambc::vec2 b) {
        return a.chebyshev_distance(b);
    }

    bool step_towards(cambc::vec2 target) {
        auto me = cambc::self();
        if (!me.can_move()) return false;

        // 1. Try built-in pathfinder
        if (me.move_towards(target)) return true;

        // 2. FALLBACK: Step into the best available direction
        std::array<cambc::vec2, 8> dirs = {
            cambc::vec2::north, cambc::vec2::north_east, cambc::vec2::east, cambc::vec2::south_east,
            cambc::vec2::south, cambc::vec2::south_west, cambc::vec2::west, cambc::vec2::north_west
        };

        const auto my_pos = me.position();
        std::sort(dirs.begin(), dirs.end(), [my_pos, target](const cambc::vec2& a, const cambc::vec2& b) {
            return (my_pos + a).chebyshev_distance(target) < (my_pos + b).chebyshev_distance(target);
        });

        for (const auto& dir : dirs) {
            if (me.move(dir)) return true;
        }

        return false;
    }

} // namespace Navigation