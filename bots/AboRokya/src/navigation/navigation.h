#pragma once
#include <cambc.h>

namespace Navigation {
    cambc::bitboard blocked_tiles();
    cambc::bitboard tiles_around(cambc::vec2 position, int radius);
    bool move_towards_closest(cambc::bitboard goals);
    bool move_away_from(cambc::vec2 danger_pos);
    bool try_traversal_dash(const cambc::distance_field& field);
}
