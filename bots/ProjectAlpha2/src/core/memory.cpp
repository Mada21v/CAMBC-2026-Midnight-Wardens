// core/memory.cpp
#include "memory.h"
#include <algorithm>
#include <vector>

namespace Memory {

namespace {

bool is_frontline_role(cambc::champion_type type) {
    return (type == cambc::champion_type::varg ||
            type == cambc::champion_type::ren ||
            type == cambc::champion_type::vendetta ||
            type == cambc::champion_type::heart_of_ruin ||
            type == cambc::champion_type::marcella ||
            type == cambc::champion_type::issac ||
            type == cambc::champion_type::ryth);
}

} // anonymous namespace

int my_slot() {
    const auto allies = cambc::champions(cambc::team::ally);
    const auto& me = cambc::self();
    for (size_t i = 0; i < allies.size(); ++i) {
        if (allies[i] == me) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

// Store camp_group_idx + 1 so that 0 means unclaimed!
void claim_camp_group(int camp_group_idx) {
    int slot = my_slot();
    if (slot >= 0 && slot < 5) {
        cambc::shared_memory[slot] = static_cast<uint8_t>(std::clamp(camp_group_idx + 1, 1, 254));
    }
}

void clear_claimed_camp() {
    int slot = my_slot();
    if (slot >= 0 && slot < 5) {
        cambc::shared_memory[slot] = 0;
    }
}

int get_ally_claimed_camp(int slot) {
    if (slot < 0 || slot >= 5) return -1;
    uint8_t val = static_cast<uint8_t>(cambc::shared_memory[slot]);
    if (val == 0 || val == 0xFF) return -1;
    return static_cast<int>(val) - 1;
}

// Byte 5:
// bits 0..4: tribute mask
// bit 5: focus_valid flag (0 = none, 1 = valid)
// bits 6..7: round tag (round & 3)
void report_enemy_focus_target(cambc::vec2 pos, float /*hp*/) {
    if (pos.x < 0 || pos.y < 0) return;
    uint8_t tag = static_cast<uint8_t>(cambc::round_number() & 0x03);
    uint8_t b5 = static_cast<uint8_t>(cambc::shared_memory[5]);
    cambc::shared_memory[5] = static_cast<uint8_t>((b5 & 0x1F) | 0x20 | (tag << 6));
    cambc::shared_memory[6] = static_cast<uint8_t>(pos.x);
    cambc::shared_memory[7] = static_cast<uint8_t>(pos.y);
}

FocusTarget get_current_focus_target() {
    FocusTarget target;
    uint8_t b5 = static_cast<uint8_t>(cambc::shared_memory[5]);
    // Bit 5 is the VALID flag
    if ((b5 & 0x20) == 0) return target;

    uint8_t tag = (b5 >> 6) & 0x03;
    uint8_t cur_tag = static_cast<uint8_t>(cambc::round_number() & 0x03);
    int age = (cur_tag >= tag) ? (cur_tag - tag) : (4 - tag + cur_tag);
    if (age > 2) {
        return target; // Stale sighting
    }

    uint8_t fx = static_cast<uint8_t>(cambc::shared_memory[6]);
    uint8_t fy = static_cast<uint8_t>(cambc::shared_memory[7]);
    if (fx == 0xFF || fy == 0xFF) return target;

    target.valid = true;
    target.pos = { static_cast<int>(fx), static_cast<int>(fy) };
    return target;
}

void clear_focus_target_if_stale() {
    uint8_t b5 = static_cast<uint8_t>(cambc::shared_memory[5]);
    if ((b5 & 0x20) == 0) return;

    uint8_t tag = (b5 >> 6) & 0x03;
    uint8_t cur_tag = static_cast<uint8_t>(cambc::round_number() & 0x03);
    int age = (cur_tag >= tag) ? (cur_tag - tag) : (4 - tag + cur_tag);
    if (age > 2) {
        cambc::shared_memory[5] = static_cast<uint8_t>(b5 & ~0x20);
        cambc::shared_memory[6] = 0xFF;
        cambc::shared_memory[7] = 0xFF;
    }
}

void set_tributing(bool tributing) {
    int slot = my_slot();
    if (slot < 0 || slot >= 5) return;
    uint8_t b5 = static_cast<uint8_t>(cambc::shared_memory[5]);
    if (tributing) {
        b5 |= static_cast<uint8_t>(1u << slot);
    } else {
        b5 &= static_cast<uint8_t>(~(1u << slot));
    }
    cambc::shared_memory[5] = b5;
}

bool is_ally_tributing(int slot) {
    if (slot < 0 || slot >= 5) return false;
    uint8_t b5 = static_cast<uint8_t>(cambc::shared_memory[5]);
    return (b5 & (1u << slot)) != 0;
}

int count_allies_tributing() {
    uint8_t b5 = static_cast<uint8_t>(cambc::shared_memory[5]);
    int count = 0;
    for (int i = 0; i < 5; ++i) {
        if (b5 & (1u << i)) count++;
    }
    return count;
}

int get_recommended_priority() {
    auto& me = cambc::self();
    int slot = my_slot();

    if (me.is_submitting_gems() || is_ally_tributing(slot)) {
        return 95;
    }
    if (is_frontline_role(me.champion_name())) {
        return 90 + slot;
    }
    return 70 + slot;
}

} // namespace Memory
