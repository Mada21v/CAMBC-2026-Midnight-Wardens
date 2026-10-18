#pragma once
#include "cambc.h"

namespace Navigation {
    // Declarations only
    int distance_squared(cambc::vec2 a, cambc::vec2 b);
    bool step_towards(cambc::vec2 target);
}