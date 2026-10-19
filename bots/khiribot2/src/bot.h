#pragma once
#include <cambc.h>
#include <array>
#include <cstdint>

// Shared-memory layout (8 bytes, read-write during match):
//   Byte 0: tribute_mask   – bit i = champion i is heading to or actively tributing
//   Byte 1: enemy_x        – x of highest-gem enemy spotted (0xFF = none)
//   Byte 2: enemy_y        – y
//   Byte 3: enemy_gems     – gems that enemy was carrying
//   Byte 4: report_round   – round_number() & 0xFF when sighting was recorded
//   Byte 5: altar_locks    – bit i = altar i has an enemy lock (as seen by any ally)
//   Byte 6: chaser_lane    – lane of champion currently chasing (0xFF = none)
//   Byte 7: status_packed  – bits [1:0] = lane0 status, bits [3:2] = lane1 status, etc.
//            Status values: 0=idle, 1=farming, 2=tributing, 3=chasing

class Bot {
    bool initialized = false;
    cambc::item_type build_item = cambc::item_type::power_shard_of_strength;
    int lane = 0;

    // Parsed from draft memory:
    int assigned_camp = -1;
    std::array<int, 64> group_altar{};   // group i → altar index
    std::array<int, 64> group_altar_d{}; // group i → chebyshev distance to altar
    int num_groups = 0;
    bool draft_parsed = false;

    // State tracking for return-to-lane after tribute
    bool just_tributed = false;
    uint32_t my_last_gem_count = 0;

    void init();
    void buy_items(bool at_spawn);

public:
    void play_turn();
};
