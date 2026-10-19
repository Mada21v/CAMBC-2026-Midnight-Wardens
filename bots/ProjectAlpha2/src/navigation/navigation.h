#pragma once
#include <cambc.h>

namespace Navigation {
    cambc::bitboard blocked_tiles();
    bool move_towards_closest(cambc::bitboard goals);
}