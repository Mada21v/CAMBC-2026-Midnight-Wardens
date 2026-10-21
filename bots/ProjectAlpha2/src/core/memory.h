// core/memory.h
#pragma once
#include <cambc.h>
#include <cstdint>

namespace Memory {

// Get this champion's unique slot index (0 .. team_size - 1)
int my_slot();

// -------------------------------------------------------------
// Camp Partitioning (Bytes 0 .. 4)
// -------------------------------------------------------------
void claim_camp_group(int camp_group_idx);
void clear_claimed_camp();
int get_ally_claimed_camp(int slot);

// -------------------------------------------------------------
// Coordinated Focus-Fire in Combat (Bytes 5 .. 9)
// -------------------------------------------------------------
struct FocusTarget {
    bool valid = false;
    cambc::vec2 pos{0, 0};
    float hp = 1e9f;
    uint8_t round = 0;
};

void report_enemy_focus_target(cambc::vec2 pos, float hp);
FocusTarget get_current_focus_target();
void clear_focus_target_if_stale();

// -------------------------------------------------------------
// Tribute Coordination (Byte 10)
// -------------------------------------------------------------
void set_tributing(bool tributing);
bool is_ally_tributing(int slot);
int count_allies_tributing();

// -------------------------------------------------------------
// Priority Management
// -------------------------------------------------------------
int get_recommended_priority();

} // namespace Memory
