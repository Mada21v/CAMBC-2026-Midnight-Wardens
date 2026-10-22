#include "draft.h"
#include <cambc.h>
#include <iostream>
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>

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

// Special case resolver for the 5th champion:
// Deterministically evaluated once at draft start so it never changes midway.
cambc::champion_draft choose_special_champion() {
    static bool resolved = false;
    static cambc::champion_draft cached_pick = cambc::champion_draft::ren;
    if (resolved) return cached_pick;

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

    // 1. Monolith is the primary/exclusive gem objective -> Stellar
    if (has_monolith && !has_normal_gem_camps) {
        cached_pick = cambc::champion_draft::stellar;
        resolved = true;
        return cached_pick;
    }

    // 2. Compact / Brawl maps (max dimension <= 28) -> Ryth (Area Stun + HP/Str tank)
    int max_dim = std::max(cambc::map::width(), cambc::map::height());
    if (max_dim <= 28) {
        cached_pick = cambc::champion_draft::ryth;
        resolved = true;
        return cached_pick;
    }

    // 3. Enemy team drafted heavy CC (2+ stun champions) -> Marcella (Stun-Immune!)
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
        cached_pick = cambc::champion_draft::marcella;
        resolved = true;
        return cached_pick;
    }

    // 4. Default: Ren (Dash mobility + Stun + Retaliation)
    cached_pick = cambc::champion_draft::ren;
    // Keep resolved = false until draft pick is finalized or round > 0 so enemy stun check can trigger if early
    if (cambc::round_number() > 0 || enemy_stuns >= 2) {
        resolved = true;
    }
    return cached_pick;
}

void play_turn() {
    if (cambc::round_number() == 0) {
        compute_and_store_opening_plan();
    }

    auto special_pick = choose_special_champion();

    // Corridor Map Topology Check (e.g. collision.map26a with narrow bottlenecks):
    bool is_corridor = (cambc::map::width() <= 25 || cambc::map::height() <= 25);

    // Guaranteed Core Team: Varg, special_pick (Ren/Ryth/Marcella/Stellar), Vesper Vale, Vendetta, Heart of Ruin.
    // Exactly 5 distinct champions in fixed priority. Heart of Ruin is ALWAYS drafted!
    std::vector<cambc::champion_draft> draft_priority;
    if (is_corridor) {
        draft_priority = {
            cambc::champion_draft::vesper_vale,
            cambc::champion_draft::varg,
            special_pick,
            cambc::champion_draft::vendetta,
            cambc::champion_draft::heart_of_ruin,
            cambc::champion_draft::ren,
            cambc::champion_draft::ryth,
            cambc::champion_draft::marcella,
            cambc::champion_draft::stellar
        };
    } else {
        draft_priority = {
            cambc::champion_draft::varg,
            special_pick,
            cambc::champion_draft::vesper_vale,
            cambc::champion_draft::vendetta,
            cambc::champion_draft::heart_of_ruin,
            cambc::champion_draft::ren,
            cambc::champion_draft::ryth,
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
