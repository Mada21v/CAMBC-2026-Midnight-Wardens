// Draft phase: pick champions and precompute camp→altar mapping and
// opening lane-to-camp assignments into draft memory.
//
// Draft Memory Layout (written here, read-only during gameplay):
//   Bytes 0-3: "MB03" magic
//   Byte 4:    num_camp_groups
//   Byte 5:    num_altars
//   Bytes 6-10: assigned opening camp index for lanes 0..4 (5 bytes)
//   Bytes 11-15: reserved
//   For each group i (2 bytes at offset 16 + 2*i):
//     [0] nearest altar index
//     [1] chebyshev distance to that altar (capped at 255)
#include <cambc.h>
#include "draft.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>

namespace {

int cheb(cambc::vec2 a, cambc::vec2 b) {
    return static_cast<int>(a.chebyshev_distance(b));
}

void write_draft_memory() {
    const auto groups = cambc::map::camp_groups();
    const auto altars = cambc::map::altars().positions();
    const auto spawn_tiles = cambc::map::spawn_region(cambc::team::ally).positions();
    const cambc::vec2 spawn = spawn_tiles.empty() ? cambc::vec2{0, 0} : spawn_tiles[0];

    const int ng = static_cast<int>(groups.size());
    const int na = static_cast<int>(altars.size());

    std::vector<uint8_t> buf(static_cast<std::size_t>(16 + ng * 2), 0);
    buf[0] = 'M'; buf[1] = 'B'; buf[2] = '0'; buf[3] = '3';
    buf[4] = static_cast<uint8_t>(ng);
    buf[5] = static_cast<uint8_t>(na);

    struct GroupDist {
        int index;
        int spawn_dist;
        int altar_idx;
        int altar_dist;
    };
    std::vector<GroupDist> gd;
    gd.reserve(ng);

    for (int gi = 0; gi < ng; ++gi) {
        int best_a = 0;
        int best_ad = 255;
        int sd = 255;
        for (auto tile : groups[gi].spawn_tiles().positions()) {
            int d_spawn = cheb(tile, spawn);
            if (d_spawn < sd) sd = d_spawn;
            for (int ai = 0; ai < na; ++ai) {
                int d_altar = cheb(tile, altars[ai]);
                if (d_altar < best_ad) {
                    best_ad = d_altar;
                    best_a = ai;
                }
            }
        }
        buf[16 + gi * 2]     = static_cast<uint8_t>(best_a);
        buf[16 + gi * 2 + 1] = static_cast<uint8_t>(std::min(best_ad, 255));
        if (groups[gi].gem_value() < 150) {
            gd.push_back({gi, sd, best_a, best_ad});
        }
    }

    // Sort by distance to ally spawn
    std::sort(gd.begin(), gd.end(), [](const GroupDist& a, const GroupDist& b) {
        return a.spawn_dist < b.spawn_dist;
    });

    for (int l = 0; l < 5; ++l) {
        if (!gd.empty())
            buf[6 + l] = static_cast<uint8_t>(gd[l % gd.size()].index);
        else
            buf[6 + l] = 0;
    }

    cambc::draft_memory = buf;
}

}  // namespace

// Fixed preference order: economy + fighters first, fragile/complex last.
// Deterministic: pick the first preference our team has not taken yet.
// Mirroring the opponent is allowed, so only skip our own picks.
void play_draft() {
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    if (chosen.empty()) {
        write_draft_memory();
    }

    constexpr std::array<cambc::champion_draft, 11> preference{
        cambc::champion_draft::cassi_and_issac,  // two champions for one pick! Always outnumber opponent
        cambc::champion_draft::varg,             // splash basic attacks + shield + reset
        cambc::champion_draft::vendetta,         // kill resets cooldowns, fastest farmer in game
        cambc::champion_draft::ryth,             // tanky area stun + reset + health adds strength
        cambc::champion_draft::marcella,         // stun immune frontline melee
        cambc::champion_draft::ren,              // stun + dash + retaliation
        cambc::champion_draft::vesper_vale,      // gold bonus + strong basics + dash + reset
        cambc::champion_draft::willow,           // team healer + reset
        cambc::champion_draft::stellar,          // reliable spell damage
        cambc::champion_draft::tavi,             // field shop + big stun
        cambc::champion_draft::prism,            // gem scaling
    };

    for (auto pick : preference) {
        if (std::find(chosen.begin(), chosen.end(), pick) == chosen.end()) {
            cambc::draft::pick(pick);
            return;
        }
    }
}
