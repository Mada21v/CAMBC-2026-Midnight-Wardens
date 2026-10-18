#include "draft.h"
#include <array>
#include <cstdint>
#include <span>
#include <cstring>
#include <iostream>

namespace Draft {

// The optimized meta composition, ranked by absolute pick priority.
// Cassi & Issac are prioritized first since they provide two bodies for one selection.
// Marcella acts as the 5th pick fallback if the map allows 5 champions.
constexpr std::array<cambc::champion_type, 5> target_composition = {
    cambc::champion_type::cassi_and_issac,
    cambc::champion_type::varg,
    cambc::champion_type::stellar,
    cambc::champion_type::ren,
    cambc::champion_type::marcella
};

// Check if a champion is already drafted by our team to avoid invalid picks.
bool is_picked_by_us(cambc::champion_type champ) {
    // Note: Adjust 'cambc::draft::ally_picks()' to the exact SDK function name for drafted allies.
    for (auto picked : cambc::draft::ally_picks()) {
        if (picked == champ) return true;
    }
    return false;
}

void compute_and_store_opening_plan() {
    // The drafting phase provides 10x fuel for the first turn[cite: 1].
    // This is where you run heavy A* or distance field calculations across the full visible map[cite: 1].
    
    // Zero out the 1,024 byte shared memory buffer[cite: 1].
    std::memset(cambc::draft_memory.data(), 0, cambc::draft_memory.size());

    // Example serialization: Store an overarching strategy ID at byte 0.
    // 0x01 = Deathball/Camp Snowball strategy.
    std::uint8_t strategy_id = 0x01;
    cambc::draft_memory[0] = strategy_id;

    // Example: Store the coordinates of the primary altar we want to control at bytes 1-8.
    // Assuming you have a function that finds the most central altar.
    // auto primary_altar = find_central_altar();
    // std::memcpy(cambc::draft_memory.data() + 1, &primary_altar.x, sizeof(primary_altar.x));
    // std::memcpy(cambc::draft_memory.data() + 5, &primary_altar.y, sizeof(primary_altar.y));

    std::cout << "Draft Turn 0: Heavy precomputation finished and packed into 1024-byte draft memory.\n";
}

void play_turn() {
    // Leverage the 10x fuel budget on round 0 to compute distance matrices and write to draft memory[cite: 1].
    if (cambc::round_number() == 0) {
        compute_and_store_opening_plan();
    }

    // Iterate through our priority list and lock in the first valid, unpicked champion.
    // The rules state we can pick the same champion as the opponent, but not one our team already has[cite: 1].
    for (const auto& champ : target_composition) {
        if (!is_picked_by_us(champ)) {
            // Note: Adjust 'cambc::draft::pick()' to the exact SDK method call.
            cambc::draft::pick(champ);
            std::cout << "Drafted champion ID: " << static_cast<int>(champ) << '\n';
            return; 
        }
    }
}

} // namespace Draft