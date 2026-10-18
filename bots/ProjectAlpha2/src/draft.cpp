#include "draft.h"
#include <cambc.h>
#include <array>
#include <cstdint>
#include <span>
#include <iostream>

namespace Draft {

// Use `champion_draft` instead of `champion_type` for the drafting phase.
constexpr std::array<cambc::champion_draft, 5> target_composition = {
    cambc::champion_draft::cassi_and_issac,
    cambc::champion_draft::varg,
    cambc::champion_draft::stellar,
    cambc::champion_draft::ren,
    cambc::champion_draft::marcella
};

// Check if our team already picked a champion
bool is_picked_by_us(cambc::champion_draft champ) {
    // SDK uses current_draft() filtered by team
    for (auto picked : cambc::draft::current_draft(cambc::team::ally)) {
        if (picked == champ) return true;
    }
    return false;
}

void compute_and_store_opening_plan() {
    // Since draft_memory_bytes is dynamically loaded from the engine, 
    // we use std::vector instead of std::array.
    std::vector<std::byte> memory(cambc::constants::draft_memory_bytes);
    
    // Flag opening strategy byte at index 0
    memory[0] = std::byte{0x01}; 

    // Submit to the engine
    cambc::draft_memory = memory;

    std::cout << "Draft Turn 0: Heavy precomputation completed.\n";
}

void play_turn() {
    if (cambc::round_number() == 0) {
        compute_and_store_opening_plan();
    }

    for (const auto& champ : target_composition) {
        if (!is_picked_by_us(champ)) {
            cambc::draft::pick(champ);
            // The SDK has a built-in to_string() for enums!
            std::cout << "Drafted champion: " << cambc::to_string(champ) << '\n';
            return;
        }
    }
}

} // namespace Draft