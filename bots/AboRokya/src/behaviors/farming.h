#pragma once
#include <cambc.h>

namespace Farming {
    void farm_optimal_camps();
    bool is_strong_carry(const cambc::champion& champ);
    bool can_clear_nearby_camp(const cambc::champion& champ);
    int closest_active_camp_dist(const cambc::champion& champ);
}
