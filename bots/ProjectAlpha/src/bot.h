#pragma once
#include <cambc.h>
#include <cstdint>

// Define the 8-byte radar layout outside the class so it is globally accessible
union TeamMemory {
    uint64_t raw;
    struct {
        uint64_t enemy_x : 7;       // Max map size 100 fits in 7 bits
        uint64_t enemy_y : 7;
        uint64_t round_spotted : 11; // Max 2000 rounds fits in 11 bits
        uint64_t is_valid : 1;
        uint64_t padding : 38;      // Reserved for future camp timers or target IDs
    } radar;
};

// Per-champion state survives between turns. Pick one stat and keep buying it.
class Bot {
    int chosen_stat = -1;

    // Spend only on power shards of the randomly chosen stat.
    void buy_items();

public:
    // Priorities: shop and recall, attack enemies in range, bank gems, then farm.
    void play_turn();
};