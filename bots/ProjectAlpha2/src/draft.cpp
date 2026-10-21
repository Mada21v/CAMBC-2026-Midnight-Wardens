#include "draft.h"
#include <cambc.h>
#include <iostream>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Draft {

// Check if our team already picked a champion
bool is_picked_by_us(cambc::champion_draft champ) {
    for (auto picked : cambc::draft::current_draft(cambc::team::ally)) {
        if (picked == champ) return true;
    }
    return false;
}

void compute_and_store_opening_plan() {
    std::vector<std::byte> memory(cambc::constants::draft_memory_bytes);
    memory[0] = std::byte{0x01}; 
    cambc::draft_memory = memory;
}

// Special case resolver between Marcella, Ren, and Stellar
// Ren is the default most probable choice ("most probably it will pick ren")
cambc::champion_draft choose_special_champion() {
    bool has_monolith = false;
    bool has_normal_gem_camps = false;
    for (const auto& group : cambc::map::camp_groups()) {
        for (const auto& c : group.camps()) {
            if (c.kind() == cambc::camp_type::monolith) {
                has_monolith = true;
            } else if (group.gem_value() > 0) {
                has_normal_gem_camps = true;
            }
        }
    }

    // Special Case 1: Monolith is the primary/exclusive gem objective -> Stellar
    if (has_monolith && !has_normal_gem_camps) {
        return cambc::champion_draft::stellar;
    }

    // Special Case 2: Enemy team drafted heavy CC (2+ stun champions) -> Marcella (Stun-Immune!)
    int enemy_stuns = 0;
    for (auto enemy_pick : cambc::draft::current_draft(cambc::team::enemy)) {
        if (enemy_pick == cambc::champion_draft::ren || 
            enemy_pick == cambc::champion_draft::ryth ||
            enemy_pick == cambc::champion_draft::tavi ||
            enemy_pick == cambc::champion_draft::cassi_and_issac) {
            enemy_stuns++;
        }
    }
    if (enemy_stuns >= 2) {
        return cambc::champion_draft::marcella;
    }

    // Default (Most probable as requested by user): Ren!
    return cambc::champion_draft::ren;
}

void play_turn() {
    if (cambc::round_number() == 0) {
        compute_and_store_opening_plan();
    }

    auto special_pick = choose_special_champion();

    // Corridor Map Topology Check (e.g. collision.map26a with narrow 1-tile bottlenecks):
    // In narrow corridors, ranged champions (Vesper Vale) dominate before melee can choke
    bool is_corridor = (cambc::map::width() <= 25 || cambc::map::height() <= 25);

    std::vector<cambc::champion_draft> draft_priority;
    if (is_corridor) {
        // Guarantee Vesper Vale ranged control at Pick 1 on tight corridor maps
        draft_priority = {
            cambc::champion_draft::vesper_vale,
            cambc::champion_draft::varg,
            special_pick,
            cambc::champion_draft::vendetta,
            cambc::champion_draft::heart_of_ruin,
            cambc::champion_draft::ren,
            cambc::champion_draft::marcella,
            cambc::champion_draft::stellar
        };
    } else {
        // Standard Top-Tier Constant Core + Special Pick
        draft_priority = {
            cambc::champion_draft::varg,
            special_pick,
            cambc::champion_draft::vesper_vale,
            cambc::champion_draft::vendetta,
            cambc::champion_draft::heart_of_ruin,
            cambc::champion_draft::ren,
            cambc::champion_draft::marcella,
            cambc::champion_draft::stellar
        };
    }

    for (const auto& champ : draft_priority) {
        if (!is_picked_by_us(champ)) {
            cambc::draft::pick(champ);
            return;
        }
    }
}

} // namespace Draft
