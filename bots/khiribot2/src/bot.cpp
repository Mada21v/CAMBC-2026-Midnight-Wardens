// Deterministic my-bot v6:
// - Aggressive movement: dashes + TP crystals during farming and returning (2-3x traversal speed)
// - Fast frequent tribute: small batches tribute immediately (1-2 turn channels, no gem accumulation/loss)
// - True double-hit attack resets (Vesper, Vendetta, Varg, Willow, Ryth) & pre-walk ranged damage on camps
// - Camp capture optimization: ward placed on capture tile during 5-round wait
// - Full 8-byte shared memory: status byte (idle/farming/tributing/chasing) to avoid duplicating camps + coordinated enemy hunting
// - Relaxed camp filtering: elks (2400 HP, 36 gems) fully farmed, only true monoliths (20000 HP) skipped
// - Return to lane after tribute: immediately walks/dashes towards assigned lane camps instead of idling
// - Enemy base raiding: attacks enemy-territory camps when enemy is dead/absent/behind
// - Full exception-safety around all vision/unit/camp queries (no error 97 aborts)
#include <cambc.h>
#include "bot.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <variant>
#include <vector>

using cambc::team;
using cambc::vec2;

namespace {

// =========================================================================
//  Constants
// =========================================================================
constexpr int CHASE_MAX_DIST          = 14;     // max distance to chase enemy via shared memory
constexpr int ENEMY_STALE_ROUNDS      = 6;      // shared-mem sighting expires after 6 rounds
constexpr float MONOLITH_HP_THRESHOLD = 10000.0f; // only monoliths have > 10,000 HP (elks are 2400)

// Status values for shared memory
constexpr uint8_t STATUS_IDLE       = 0;
constexpr uint8_t STATUS_FARMING    = 1;
constexpr uint8_t STATUS_TRIBUTING  = 2;
constexpr uint8_t STATUS_CHASING    = 3;

// =========================================================================
//  Shared-memory helpers (8 bytes total)
//  Byte 0: tribute_mask (bits 0-4 for lanes 0-4) + lane 4 status (bits 6-7)
//  Bytes 1-2: enemy_x, enemy_y (0xFF = none)
//  Byte 3: enemy_gems
//  Byte 4: report_round
//  Byte 5: altar_locks
//  Byte 6: chaser_lane
//  Byte 7: status byte (lanes 0-3 status, 2 bits each)
// =========================================================================
struct SMem {
    uint8_t data[8]{};

    // --- tribute mask (byte 0, bits 0-4) ---
    bool is_tributing(int lane) const {
        if (lane < 0 || lane >= 5) return false;
        return (data[0] >> lane) & 1;
    }
    void set_tributing(int lane, bool v) {
        if (lane < 0 || lane >= 5) return;
        if (v) data[0] |= static_cast<uint8_t>(1u << lane);
        else   data[0] &= static_cast<uint8_t>(~(1u << lane));
    }
    int tribute_count() const {
        int c = 0;
        for (int i = 0; i < 5; ++i) c += is_tributing(i);
        return c;
    }

    // --- enemy sighting (bytes 1-4) ---
    bool has_enemy() const { return data[1] != 0xFF; }
    vec2 enemy_pos() const { return {data[1], data[2]}; }
    int  enemy_gems() const { return data[3]; }
    uint8_t report_round() const { return data[4]; }
    void set_enemy(vec2 pos, int gems, uint8_t rnd) {
        data[1] = static_cast<uint8_t>(pos.x);
        data[2] = static_cast<uint8_t>(pos.y);
        data[3] = static_cast<uint8_t>(gems);
        data[4] = rnd;
    }
    void clear_enemy() { data[1] = 0xFF; data[2] = 0xFF; data[3] = 0; }
    bool enemy_is_stale(uint8_t cur_round) const {
        int age = (cur_round >= data[4])
                    ? (cur_round - data[4])
                    : (256 - data[4] + cur_round);
        return age > ENEMY_STALE_ROUNDS;
    }

    // --- team carried gems (bytes 5 and 6) ---
    // Byte 5 has low 8 bits (0..255). Byte 6 bits 4-5 have high 2 bits (256, 512).
    // Total range: 0..1023 gems across all champions.
    uint16_t team_carried_gems() const {
        return static_cast<uint16_t>(data[5]) | (static_cast<uint16_t>((data[6] >> 4) & 0x3) << 8);
    }
    void set_team_carried_gems(uint16_t v) {
        if (v > 1023) v = 1023;
        data[5] = static_cast<uint8_t>(v & 0xFF);
        data[6] = static_cast<uint8_t>((data[6] & 0xCF) | (((v >> 8) & 0x3) << 4));
    }

    // --- chaser lane (byte 6 bits 0-3: 0=none, 1..5 for lanes 0..4) ---
    int chaser_lane() const {
        uint8_t c = data[6] & 0x0F;
        return (c >= 1 && c <= 5) ? (c - 1) : -1;
    }
    void set_chaser(int lane) {
        data[6] = static_cast<uint8_t>((data[6] & 0xF0) | (static_cast<uint8_t>(lane + 1) & 0x0F));
    }
    void clear_chaser_if(int lane) {
        if (chaser_lane() == lane)
            data[6] &= 0xF0;
    }

    // --- status byte (byte 7 for lanes 0-3, byte 0 bits 6-7 for lane 4) ---
    uint8_t get_status(int lane) const {
        if (lane < 0 || lane >= 5) return STATUS_IDLE;
        if (lane < 4) return (data[7] >> (lane * 2)) & 0x3;
        return (data[0] >> 6) & 0x3;
    }
    void set_status(int lane, uint8_t status) {
        if (lane < 0 || lane >= 5) return;
        if (lane < 4) {
            uint8_t mask = static_cast<uint8_t>(0x3 << (lane * 2));
            data[7] = static_cast<uint8_t>((data[7] & ~mask) | ((status & 0x3) << (lane * 2)));
        } else {
            uint8_t mask = static_cast<uint8_t>(0x3 << 6);
            data[0] = static_cast<uint8_t>((data[0] & ~mask) | ((status & 0x3) << 6));
        }
    }
    int count_with_status(uint8_t status) const {
        int c = 0;
        for (int i = 0; i < 5; ++i)
            if (get_status(i) == status) ++c;
        return c;
    }

    static SMem read() {
        SMem s;
        cambc::shared_memory.read(0, std::span<uint8_t>{s.data, 8});
        return s;
    }
    void write() const {
        cambc::shared_memory.write(0, std::span<const uint8_t>{data, 8});
    }
};

// =========================================================================
//  Geometry / pathfinding
// =========================================================================
int cheb(vec2 a, vec2 b) {
    return static_cast<int>(a.chebyshev_distance(b));
}

cambc::bitboard tiles_around(vec2 position, int radius) {
    cambc::bitboard tiles;
    tiles.clear();
    tiles.draw_chebyshev(position, radius);
    return tiles;
}

cambc::bitboard blocked_tiles() {
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(team::enemy) | cambc::vision::champion_tiles()
        | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    blocked.set(cambc::self().position(), false);
    return blocked;
}

bool move_towards_closest(cambc::bitboard goals) {
    auto& me = cambc::self();
    try {
        if (!me.can_move() || goals.is_set(me.position()))
            return false;
        const auto blocked = blocked_tiles();
        goals &= ~blocked | cambc::vision::camp_tiles();
        if (goals.is_empty())
            return false;
        if (goals.count() == 1 && !blocked.is_set(goals.positions()[0])) {
            try {
                return me.move_towards(goals.positions()[0]);
            } catch (...) {
                return false;
            }
        }

        const auto path_walls = blocked & ~goals;
        cambc::distance_field field;
        cambc::bitboard visited;
        field.compute_to(path_walls, goals, tiles_around(me.position(), 0), visited);

        auto step = me.position();
        for (auto tile : blocked.reachable_tiles(step, me.base_stats().base_move_speed).positions()) {
            if (field.get(tile) < field.get(step))
                step = tile;
        }
        if (step != me.position() && !blocked.is_set(step)) {
            try {
                return me.move_absolute(step);
            } catch (...) {
                return false;
            }
        }
        return false;
    } catch (...) {
        return false;
    }
}

bool move_towards_pos(vec2 target) {
    cambc::bitboard goal;
    goal.clear();
    goal.set(target, true);
    goal.draw_chebyshev(target, 1);
    return move_towards_closest(goal);
}

// =========================================================================
//  Exception-safe helpers for camps and enemies
// =========================================================================
bool is_safe_farmable_camp(const cambc::camp& c) {
    try {
        if (!c.is_visible()) return false;
        if (c.is_monolith()) return false;
        if (c.max_health() > MONOLITH_HP_THRESHOLD) return false;
        return c.health() > 0.0f;
    } catch (...) {
        return false;
    }
}

bool is_safe_enemy_target(const cambc::champion& e) {
    try {
        return e.is_visible() && e.is_alive() && e.health() > 0.0f;
    } catch (...) {
        return false;
    }
}

bool is_group_respawning(const cambc::map::camp_group& group, const cambc::bitboard& visible) {
    try {
        if ((group.spawn_tiles() & visible).is_empty()) return false;
        return std::holds_alternative<cambc::map::camp_group_respawning>(group.state());
    } catch (...) {
        return false;
    }
}

bool is_group_reward_available(const cambc::map::camp_group& group, const cambc::bitboard& visible) {
    try {
        if ((group.spawn_tiles() & visible).is_empty()) return false;
        return std::holds_alternative<cambc::map::camp_group_reward_available>(group.state());
    } catch (...) {
        return false;
    }
}

// =========================================================================
//  [Fix #1] Dash toward a target position (legal unblocked landing tiles)
// =========================================================================
bool try_dash_toward(vec2 target) {
    auto& me = cambc::self();
    if (!me.can_cast()) return false;
    for (auto& abil : me.abilities()) {
        try {
            if (!abil.is_ready() || !abil.is_dash()) continue;
            if (!abil.can_target_ground()) continue;

            cambc::bitboard valid;
            try { valid = abil.valid_tiles(); } catch (...) { continue; }
            const auto blocked = blocked_tiles();
            const auto visible = cambc::vision::visible_tiles();
            valid &= ~blocked & visible;
            if (valid.is_empty()) continue;

            vec2 best = me.position();
            int best_d = cheb(me.position(), target);
            for (auto tile : valid.positions()) {
                int d = cheb(tile, target);
                if (d < best_d) { best_d = d; best = tile; }
            }
            if (best != me.position()) {
                try {
                    if (abil.use(best)) return true;
                } catch (...) {}
            }
        } catch (...) {}
    }
    return false;
}

// =========================================================================
//  [Fix #1] TP crystal toward target (jump 3 tiles ignoring terrain)
// =========================================================================
bool try_tp_toward(vec2 target) {
    auto& me = cambc::self();
    try {
        auto& inv = me.consumables();
        if (inv[cambc::consumable_type::tp_crystal].count() == 0)
            return false;

        const auto blocked = blocked_tiles();
        const auto visible = cambc::vision::visible_tiles();

        vec2 best = me.position();
        int best_d = cheb(me.position(), target);
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dy = -3; dy <= 3; ++dy) {
                if (std::max(std::abs(dx), std::abs(dy)) > 3) continue;
                vec2 dest{me.position().x + dx, me.position().y + dy};
                if (!cambc::map::is_in_bounds(dest)) continue;
                if (!visible.is_set(dest) || blocked.is_set(dest)) continue;
                int d = cheb(dest, target);
                if (d < best_d) { best_d = d; best = dest; }
            }
        }

        if (best != me.position()) {
            try {
                return inv[cambc::consumable_type::tp_crystal].use(best);
            } catch (...) {}
        }
    } catch (...) {}
    return false;
}

// =========================================================================
//  [Fix #3] True double-hit attack with reset (Vesper, Vendetta, Varg, Willow, Ryth)
// =========================================================================
template <class Target>
bool attack_target(const Target& target) {
    auto& me = cambc::self();
    try {
        if (!target.is_visible() || target.health() <= 0
            || !me.attack_tiles().is_set(target.position()))
            return false;

        // 1. Initial basic attack
        if (me.can_attack()) {
            try {
                me.attack(target);
            } catch (...) {
                return false;
            }
        }

        // 2. Attack-reset if available
        if (me.can_cast()) {
            for (auto& abil : me.abilities()) {
                try {
                    if (abil.is_ready() && abil.is_attack_reset() && abil.can_target_self()) {
                        if (abil.use()) {
                            // 3. Second hit (double-hit!)
                            try {
                                if (me.can_attack() && target.is_visible() && target.health() > 0) {
                                    me.attack(target);
                                }
                            } catch (...) {}
                            break;
                        }
                    }
                } catch (...) {}
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool attack_altar_lock(const cambc::altar_lock& lock) {
    auto& me = cambc::self();
    try {
        if (!me.attack_tiles().is_set(lock.position()) || lock.hits_remaining() == 0)
            return false;
        if (me.can_attack()) {
            try {
                me.attack(lock);
            } catch (...) {
                return false;
            }
        }
        if (me.can_cast()) {
            for (auto& abil : me.abilities()) {
                try {
                    if (abil.is_ready() && abil.is_attack_reset() && abil.can_target_self()) {
                        if (abil.use()) {
                            try {
                                if (me.can_attack() && lock.hits_remaining() > 0)
                                    me.attack(lock);
                            } catch (...) {}
                            break;
                        }
                    }
                } catch (...) {}
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ability_excludes_move(const cambc::ability& abil) {
    try {
        const auto& opt = abil.info();
        if (!opt) return false;
        return std::visit([](const auto& v) { return v.exclusive_with_movement; }, *opt);
    } catch (...) { return false; }
}

// =========================================================================
//  Roster / build helpers
// =========================================================================
int my_lane() {
    const auto allies = cambc::champions(team::ally);
    const auto& me = cambc::self();
    for (std::size_t i = 0; i < allies.size(); ++i)
        if (allies[i] == me) return static_cast<int>(i);
    return 0;
}

cambc::item_type build_for(cambc::champion_type t) {
    using ct = cambc::champion_type;
    using it = cambc::item_type;
    switch (t) {
        case ct::vesper_vale: return it::treasure_shard_of_strength;
        case ct::vendetta:
        case ct::ren:
        case ct::varg:
        case ct::issac:     return it::monster_shard_of_strength;
        case ct::ryth:
        case ct::marcella:  return it::jungle_shard_of_health;
        case ct::willow:
        case ct::stellar:
        case ct::cassi:
        case ct::prism:
        case ct::tavi:      return it::power_shard_of_focus;
        default:            return it::power_shard_of_strength;
    }
}

int priority_for(cambc::champion_type t, bool carrying) {
    if (carrying) return 100;
    using ct = cambc::champion_type;
    switch (t) {
        case ct::willow:      return 90;
        case ct::vesper_vale: return 70;
        case ct::vendetta:    return 65;
        case ct::ren:         return 60;
        case ct::varg:        return 58;
        case ct::ryth:        return 55;
        case ct::marcella:    return 54;
        case ct::stellar:     return 45;
        case ct::prism:       return 44;
        case ct::tavi:        return 43;
        case ct::cassi:       return 42;
        case ct::issac:       return 41;
        default:              return 50;
    }
}

// =========================================================================
//  [Fix #3] Ranged damage spells fired on enemies OR camps BEFORE walking
// =========================================================================
bool try_ranged_damage_spells() {
    auto& me = cambc::self();
    if (!me.can_cast()) return false;
    for (auto& abil : me.abilities()) {
        try {
            if (!abil.is_ready()) continue;
            if (!abil.is_damage() && !abil.is_cc() && !abil.is_aoe_damage()) continue;

            cambc::bitboard range;
            try { range = abil.valid_tiles(); } catch (...) { continue; }

            // 1. Target enemies first
            if (abil.can_target_enemies()) {
                std::optional<cambc::champion> best_e;
                for (const auto& e : cambc::vision::champions(team::enemy)) {
                    if (!is_safe_enemy_target(e)) continue;
                    try {
                        if (!range.is_set(e.position())) continue;
                        if (!best_e || e.health() < best_e->health())
                            best_e = e;
                    } catch (...) {}
                }
                if (best_e) {
                    try {
                        if (best_e->is_visible() && best_e->health() > 0)
                            if (abil.use(*best_e)) return true;
                    } catch (...) {}
                }
            }

            // 2. Target camps if no enemies (speed up fight time before walking!)
            if (abil.can_target_camps()) {
                std::optional<cambc::camp> best_c;
                for (const auto& c : cambc::vision::camps()) {
                    if (!is_safe_farmable_camp(c)) continue;
                    try {
                        if (!range.is_set(c.position())) continue;
                        if (!best_c || c.health() < best_c->health())
                            best_c = c;
                    } catch (...) {}
                }
                if (best_c) {
                    try {
                        if (best_c->is_visible() && best_c->health() > 0)
                            if (abil.use(*best_c)) return true;
                    } catch (...) {}
                }
            }
        } catch (...) {}
    }
    return false;
}

// =========================================================================
//  [Fix #3] Support abilities: heal, shield, and attack-resets
// =========================================================================
void try_support_abilities() {
    auto& me = cambc::self();
    if (!me.can_cast()) return;
    for (auto& abil : me.abilities()) {
        if (!me.can_cast()) return;
        try {
            if (!abil.is_ready() || ability_excludes_move(abil))
                continue;

            // Attack reset when target in range
            if (abil.is_attack_reset() && abil.can_target_self()) {
                bool near = false;
                for (const auto& e : cambc::vision::champions(team::enemy))
                    if (is_safe_enemy_target(e) && me.attack_tiles().is_set(e.position())) { near = true; break; }
                if (!near)
                    for (const auto& c : cambc::vision::camps())
                        if (is_safe_farmable_camp(c) && me.attack_tiles().is_set(c.position())) { near = true; break; }
                if (near && abil.use()) {
                    // Double hit immediately!
                    if (me.can_attack()) {
                        for (const auto& e : cambc::vision::champions(team::enemy)) {
                            try {
                                if (is_safe_enemy_target(e) && me.attack_tiles().is_set(e.position())) {
                                    me.attack(e);
                                    break;
                                }
                            } catch (...) {}
                        }
                    }
                    if (me.can_attack()) {
                        for (const auto& c : cambc::vision::camps()) {
                            try {
                                if (is_safe_farmable_camp(c) && me.attack_tiles().is_set(c.position())) {
                                    me.attack(c);
                                    break;
                                }
                            } catch (...) {}
                        }
                    }
                    return;
                }
                continue;
            }

            // Shield when below 75% health
            if (abil.is_shield() && abil.can_target_self()) {
                if (me.health() >= me.max_health() * 0.75f) continue;
                bool threat = !cambc::vision::champions(team::enemy).empty()
                           || !cambc::vision::camps().empty();
                if (threat && abil.use()) return;
                continue;
            }

            // Heal lowest-health ally
            if (abil.is_heal() && abil.can_target_allies()) {
                cambc::bitboard range;
                try { range = abil.valid_tiles(); } catch (...) { continue; }
                std::optional<cambc::champion> worst;
                for (const auto& a : cambc::vision::champions(team::ally)) {
                    try {
                        if (a == me || !a.is_visible() || a.health() <= 0) continue;
                        if (a.health() >= a.max_health() * 0.85f) continue;
                        if (!range.is_set(a.position())) continue;
                        if (!worst || (a.max_health() - a.health()) > (worst->max_health() - worst->health()))
                            worst = a;
                    } catch (...) {}
                }
                if (worst && abil.use(*worst)) return;
                continue;
            }
        } catch (...) {}
    }
}

// =========================================================================
//  [Fix #4] Ward placement with camp capture optimization
// =========================================================================
void maybe_ward() {
    auto& me = cambc::self();
    try {
        if (me.ward_cooldown() != 0) return;
        if (!me.ward_positions().empty()) return;
        const auto visible = cambc::vision::visible_tiles();
        auto placement = me.ward_placement_tiles() & visible;
        if (placement.is_empty()) return;

        // [Fix #4] Place ward on capture tile during 5-round capture wait
        for (const auto& group : cambc::map::camp_groups()) {
            if (is_group_reward_available(group, visible)) {
                for (auto tile : group.spawn_tiles().positions()) {
                    if (placement.is_set(tile)) {
                        try {
                            me.place_ward(tile);
                            return;
                        } catch (...) {}
                    }
                }
            }
        }

        // Fallback: ward nearest altar approach
        vec2 anchor = me.position();
        int best = std::numeric_limits<int>::max();
        for (auto altar : cambc::map::altars().positions()) {
            int d = cheb(me.position(), altar);
            if (d < best) { best = d; anchor = altar; }
        }
        if (best == std::numeric_limits<int>::max()) {
            for (const auto& c : cambc::vision::camps()) {
                try {
                    int d = cheb(me.position(), c.position());
                    if (d < best) { best = d; anchor = c.position(); }
                } catch (...) {}
            }
        }
        for (auto tile : placement.positions()) {
            if (cheb(tile, anchor) < cheb(me.position(), anchor)) {
                try {
                    me.place_ward(tile);
                    return;
                } catch (...) {}
            }
        }
    } catch (...) {}
}

// =========================================================================
//  Altar status helper (distinguish enemy lock vs ally lock vs unlocked)
// =========================================================================
struct AltarInfo {
    vec2 pos{};
    bool has_lock = false;
    bool is_enemy_lock = false;
    bool is_ally_lock = false;
    std::optional<cambc::altar_lock> lock_obj;
};

AltarInfo get_altar_info(vec2 altar) {
    AltarInfo info;
    info.pos = altar;
    try {
        if (cambc::vision::has_altar_lock(altar)) {
            info.has_lock = true;
            auto lock = cambc::vision::altar_lock_at(altar);
            info.lock_obj = lock;
            if (lock.is_enemy()) info.is_enemy_lock = true;
            else if (lock.is_ally()) info.is_ally_lock = true;
        }
    } catch (...) {}
    return info;
}

// =========================================================================
//  Scan visible enemies / altars and update shared memory (Exception-Safe)
// =========================================================================
void scan_enemies_and_altars(SMem& sm) {
    const uint8_t cur_round = static_cast<uint8_t>(cambc::round_number() & 0xFF);

    if (sm.has_enemy() && sm.enemy_is_stale(cur_round))
        sm.clear_enemy();

    vec2 best_pos{0xFF, 0xFF};
    int best_gems = 0;
    float best_hp = 99999.0f;
    bool found = false;

    for (const auto& e : cambc::vision::champions(team::enemy)) {
        try {
            if (!is_safe_enemy_target(e)) continue;
            int g = static_cast<int>(e.gem_count());
            float hp = e.health();
            vec2 p = e.position();
            if (!found || g > best_gems || (g == best_gems && hp < best_hp)) {
                best_gems = g;
                best_hp = hp;
                best_pos = p;
                found = true;
            }
        } catch (...) {}
    }
    if (found && best_gems > 0) {
        if (!sm.has_enemy() || best_gems > sm.enemy_gems()) {
            sm.set_enemy(best_pos, best_gems, cur_round);
        }
    }
}

// Distance to nearest altar
int dist_to_closest_altar(vec2 pos) {
    int min_d = 999;
    for (auto a : cambc::map::altars().positions()) {
        int d = cheb(pos, a);
        if (d < min_d) min_d = d;
    }
    return min_d;
}

// Whether any farmable camps are in range
bool has_farmable_camps_nearby(vec2 pos, int max_dist) {
    for (const auto& c : cambc::vision::camps()) {
        try {
            if (is_safe_farmable_camp(c) && cheb(pos, c.position()) <= max_dist)
                return true;
        } catch (...) {}
    }
    return false;
}

// Smart tribute decision: tribute after clearing a camp, near altars, or when winning
bool should_tribute(const cambc::champion& me, uint32_t total_team_gems) {
    uint32_t gems = me.gem_count();
    if (gems == 0) return false;

    // 1. Winning condition: sprint to altar immediately
    uint32_t submitted = cambc::global_submitted_gems(team::ally);
    if (total_team_gems >= 1000 || submitted + gems >= 1000)
        return true;

    // 2. If an active camp reward is right next to us (<= 4 tiles), finish capture first
    try {
        const auto visible = cambc::vision::visible_tiles();
        for (const auto& group : cambc::map::camp_groups()) {
            if (is_group_reward_available(group, visible)) {
                for (auto t : group.spawn_tiles().positions()) {
                    if (cheb(me.position(), t) <= 4)
                        return false;
                }
            }
        }
    } catch (...) {}

    // 3. Bank carried gems immediately!
    return true;
}

// =========================================================================
//  [Fix #7] Return to lane goal: find assigned or nearest allied camp
// =========================================================================
vec2 nearest_camp_goal(int assigned_camp) {
    auto& me = cambc::self();
    const auto groups = cambc::map::camp_groups();
    vec2 best_pos = me.position();
    int best_d = std::numeric_limits<int>::max();

    if (assigned_camp >= 0 && assigned_camp < static_cast<int>(groups.size())) {
        for (auto tile : groups[assigned_camp].spawn_tiles().positions()) {
            int d = cheb(me.position(), tile);
            if (d < best_d) { best_d = d; best_pos = tile; }
        }
    }

    if (best_d == std::numeric_limits<int>::max()) {
        for (const auto& group : groups) {
            if (group.gem_value() >= 150) continue;
            for (auto tile : group.spawn_tiles().positions()) {
                int d = cheb(me.position(), tile);
                if (d < best_d) { best_d = d; best_pos = tile; }
            }
        }
    }
    return best_pos;
}

// =========================================================================
//  TRIBUTE: Channel at altar (Exception-safe)
// =========================================================================
bool try_tribute(SMem& sm, int lane) {
    auto& me = cambc::self();
    if (me.gem_count() == 0) return false;

    // 1. Attack enemy locks in attack range first (double-hit with resets!)
    if (me.can_attack()) {
        for (const auto& lock : cambc::vision::altar_locks()) {
            try {
                if (lock.is_enemy() && me.attack_tiles().is_set(lock.position())) {
                    attack_altar_lock(lock);
                    me.set_priority(100);
                    sm.set_tributing(lane, true);
                    sm.set_status(lane, STATUS_TRIBUTING);
                    return true;
                }
            } catch (...) {}
        }
    }

    // 2. Are we in submit range (<= 1) of an altar?
    for (auto altar : cambc::map::altars().positions()) {
        if (cheb(me.position(), altar) <= cambc::constants::submit_gems_range) {
            auto ainfo = get_altar_info(altar);
            if (ainfo.is_enemy_lock) {
                if (ainfo.lock_obj) {
                    attack_altar_lock(*ainfo.lock_obj);
                    me.set_priority(100);
                    sm.set_tributing(lane, true);
                    sm.set_status(lane, STATUS_TRIBUTING);
                    return true;
                }
            } else {
                try {
                    me.submit_gems(altar);
                    me.set_priority(100);
                    sm.set_tributing(lane, true);
                    sm.set_status(lane, STATUS_TRIBUTING);
                    return true;
                } catch (...) {}
            }
        }
    }

    // 3. Navigation to altar approach tiles
    cambc::bitboard all_goals;
    all_goals.clear();
    cambc::bitboard friendly_goals;
    friendly_goals.clear();

    for (auto altar : cambc::map::altars().positions()) {
        auto approach = tiles_around(altar, cambc::constants::submit_gems_range);
        all_goals |= approach;
        auto ainfo = get_altar_info(altar);
        if (!ainfo.is_enemy_lock) {
            friendly_goals |= approach;
        }
    }

    cambc::bitboard target_goals = friendly_goals.is_empty() ? all_goals : friendly_goals;

    // Dash towards closest altar approach tile if distance > 1
    vec2 best_approach = me.position();
    int min_d = 999;
    for (auto a : target_goals.positions()) {
        int d = cheb(me.position(), a);
        if (d < min_d) { min_d = d; best_approach = a; }
    }
    if (min_d > 1 && best_approach != me.position()) {
        try_dash_toward(best_approach);
    }

    // Move toward altar approach tiles
    move_towards_closest(target_goals);

    // After moving, if in submit range, submit or attack lock!
    for (auto altar : cambc::map::altars().positions()) {
        if (cheb(me.position(), altar) <= cambc::constants::submit_gems_range) {
            auto ainfo = get_altar_info(altar);
            if (!ainfo.is_enemy_lock) {
                try {
                    me.submit_gems(altar);
                    break;
                } catch (...) {}
            } else if (ainfo.lock_obj) {
                attack_altar_lock(*ainfo.lock_obj);
                break;
            }
        }
    }

    // Attack locks or enemies in range after moving
    if (me.can_attack()) {
        for (const auto& lock : cambc::vision::altar_locks()) {
            try {
                if (lock.is_enemy() && me.attack_tiles().is_set(lock.position())) {
                    attack_altar_lock(lock);
                    break;
                }
            } catch (...) {}
        }
        if (me.can_attack()) {
            for (const auto& enemy : cambc::vision::champions(team::enemy)) {
                if (attack_target(enemy)) break;
            }
        }
    }

    try_support_abilities();
    me.set_priority(100);
    sm.set_tributing(lane, true);
    sm.set_status(lane, STATUS_TRIBUTING);
    return true;
}

// =========================================================================
//  [Fix #5] CHASE: Coordinated hunting of enemy gem carriers
// =========================================================================
bool try_chase_enemy(SMem& sm, int lane) {
    auto& me = cambc::self();

    if (me.health() < me.max_health() * 0.40f) return false;
    if (me.gem_count() > 0) return false;

    // Check visible enemies carrying gems
    std::optional<cambc::champion> target;
    for (const auto& e : cambc::vision::champions(team::enemy)) {
        try {
            if (!is_safe_enemy_target(e) || e.gem_count() == 0) continue;
            if (!target || e.gem_count() > target->gem_count()
                || (e.gem_count() == target->gem_count() && e.health() < target->health()))
                target = e;
        } catch (...) {}
    }

    vec2 chase_pos{};
    if (target) {
        chase_pos = target->position();
        sm.set_chaser(lane);
    } else if (sm.has_enemy() && sm.enemy_gems() > 0) {
        const uint8_t cur = static_cast<uint8_t>(cambc::round_number() & 0xFF);
        if (!sm.enemy_is_stale(cur)) {
            int d = cheb(me.position(), sm.enemy_pos());
            // [Fix #5] Allow allies to coordinate chase
            if (d <= CHASE_MAX_DIST) {
                chase_pos = sm.enemy_pos();
                if (sm.chaser_lane() == -1) sm.set_chaser(lane);
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }

    sm.set_status(lane, STATUS_CHASING);

    // [Fix #3] Ranged damage spell on enemy before walking
    try_ranged_damage_spells();

    // Attack target if already in range
    if (target && attack_target(*target)) {
        try_support_abilities();
        me.set_priority(priority_for(me.champion_name(), false));
        return true;
    }

    // [Fix #1] Dash towards enemy
    try_dash_toward(chase_pos);

    // Move toward enemy
    move_towards_pos(chase_pos);

    // Attack after moving (focus fire lowest HP enemy in attack range)
    std::optional<cambc::champion> post_enemy;
    float post_lowest_hp = 999999.0f;
    for (const auto& e : cambc::vision::champions(team::enemy)) {
        try {
            if (!is_safe_enemy_target(e)) continue;
            if (me.attack_tiles().is_set(e.position())) {
                float hp = e.health();
                if (hp < post_lowest_hp) {
                    post_lowest_hp = hp;
                    post_enemy = e;
                }
            }
        } catch (...) {}
    }
    if (post_enemy) {
        if (attack_target(*post_enemy))
            try_support_abilities();
    }

    me.set_priority(priority_for(me.champion_name(), false));
    return true;
}

// =========================================================================
//  [Fix #1, #3, #5, #6, #8] Farming & Base Raiding
// =========================================================================
void farm_camps(int lane, int assigned_camp, const SMem& sm, bool can_win_now = false) {
    (void)lane;
    (void)sm;
    auto& me = cambc::self();
    const auto visible = cambc::vision::visible_tiles();
    const auto groups = cambc::map::camp_groups();

    // [Fix #3] Ranged spell on visible camps before walking
    try_ranged_damage_spells();

    // [Fix #6] Attack any visible non-monolith camp in range with focus fire on lowest HP
    std::optional<cambc::camp> best_camp;
    float lowest_hp = 999999.0f;
    for (const auto& camp : cambc::vision::camps()) {
        try {
            if (!is_safe_farmable_camp(camp)) continue;
            if (me.attack_tiles().is_set(camp.position())) {
                float hp = camp.health();
                if (hp < lowest_hp) {
                    lowest_hp = hp;
                    best_camp = camp;
                }
            }
        } catch (...) {}
    }
    if (best_camp) {
        if (attack_target(*best_camp)) {
            try_support_abilities();
            maybe_ward();
            return;
        }
    }

    // Check if any camp group in vision has reward available to capture
    if (!can_win_now) {
        for (const auto& group : groups) {
            if (is_group_reward_available(group, visible)) {
                if (group.spawn_tiles().is_set(me.position())) {
                    // Standing on capture tile: fight any enemies in range and stay
                    for (const auto& e : cambc::vision::champions(team::enemy)) {
                        if (attack_target(e)) {
                            try_support_abilities();
                            break;
                        }
                    }
                    maybe_ward();
                    return;
                }
                // If an ally is already standing on this capture tile, do not crowd it!
                bool ally_there = false;
                for (const auto& a : cambc::vision::champions(team::ally)) {
                    try {
                        if (a != me && group.spawn_tiles().is_set(a.position())) {
                            ally_there = true;
                            break;
                        }
                    } catch (...) {}
                }
                if (ally_there) continue;

                // Near capture tile: dash/move directly onto it!
                cambc::bitboard valid_spawn = group.spawn_tiles() & ~blocked_tiles() & visible;
                if (!valid_spawn.is_empty()) {
                    vec2 best_tile = me.position();
                    int min_d = 999;
                    for (auto t : valid_spawn.positions()) {
                        int d = cheb(me.position(), t);
                        if (d < min_d) { min_d = d; best_tile = t; }
                    }
                    if (min_d <= 6) {
                        if (min_d > 1) try_dash_toward(best_tile);
                        move_towards_closest(valid_spawn);
                        for (const auto& e : cambc::vision::champions(team::enemy)) {
                            if (attack_target(e)) {
                                try_support_abilities();
                                break;
                            }
                        }
                        maybe_ward();
                        return;
                    }
                }
            }
        }
    }

    // Endgame sprint: when total team gems >= 1000, 0-gem champions escort altars to secure victory
    if (can_win_now) {
        cambc::bitboard altar_goals;
        altar_goals.clear();
        for (auto a : cambc::map::altars().positions()) {
            altar_goals |= tiles_around(a, cambc::constants::submit_gems_range);
        }
        if (!altar_goals.is_empty()) {
            move_towards_closest(altar_goals);
            for (const auto& lock : cambc::vision::altar_locks()) {
                if (lock.is_enemy() && me.attack_tiles().is_set(lock.position())) {
                    attack_altar_lock(lock);
                    return;
                }
            }
            for (const auto& e : cambc::vision::champions(team::enemy)) {
                if (attack_target(e)) return;
            }
            return;
        }
    }

    // If there are visible farmable camps, move towards them and attack!
    cambc::bitboard visible_camp_goals;
    visible_camp_goals.clear();
    for (const auto& camp : cambc::vision::camps()) {
        try {
            if (!is_safe_farmable_camp(camp)) continue;
            auto approach = tiles_around(camp.position(), me.attack_range());
            visible_camp_goals |= approach;
            visible_camp_goals.set(camp.position(), true);
        } catch (...) {}
    }

    if (!visible_camp_goals.is_empty()) {
        vec2 best_c_target = me.position();
        int min_cd = 999;
        for (auto tile : visible_camp_goals.positions()) {
            int d = cheb(me.position(), tile);
            if (d < min_cd) { min_cd = d; best_c_target = tile; }
        }
        if (min_cd > 1 && best_c_target != me.position()) {
            try_dash_toward(best_c_target);
        }
        move_towards_closest(visible_camp_goals);

        std::optional<cambc::camp> post_camp;
        float post_min_hp = 999999.0f;
        for (const auto& camp : cambc::vision::camps()) {
            try {
                if (!is_safe_farmable_camp(camp)) continue;
                if (me.attack_tiles().is_set(camp.position())) {
                    float hp = camp.health();
                    if (hp < post_min_hp) {
                        post_min_hp = hp;
                        post_camp = camp;
                    }
                }
            } catch (...) {}
        }
        if (post_camp) {
            if (attack_target(*post_camp)) {
                try_support_abilities();
                maybe_ward();
                return;
            }
        }
        return;
    }

    // Build camp goals: no camps in direct vision, path to nearest available camp
    cambc::bitboard all_goals;
    all_goals.clear();
    cambc::bitboard opening_goals;
    opening_goals.clear();

    const bool opening = cambc::round_number() < 60;
    if (opening && assigned_camp >= 0 && assigned_camp < static_cast<int>(groups.size())) {
        const auto& ag = groups[assigned_camp];
        if (ag.gem_value() < 150 && !is_group_respawning(ag, visible)) {
            opening_goals |= ag.spawn_tiles();
        }
    }

    for (const auto& group : groups) {
        if (group.gem_value() >= 150) continue;
        if (is_group_respawning(group, visible)) continue;
        all_goals |= group.spawn_tiles();
    }

    cambc::bitboard goals = (!opening_goals.is_empty()) ? opening_goals : all_goals;
    if (goals.is_empty()) return;

    // Find nearest camp goal
    vec2 best_target{};
    int min_d = std::numeric_limits<int>::max();
    for (auto tile : goals.positions()) {
        int d = cheb(me.position(), tile);
        if (d < min_d) { min_d = d; best_target = tile; }
    }

    // [Fix #1] Dash toward camp if distance > 1
    if (min_d > 1) {
        try_dash_toward(best_target);
    }

    // Walk toward closest camp goal
    move_towards_closest(goals);

    std::optional<cambc::camp> goal_camp;
    float goal_min_hp = 999999.0f;
    for (const auto& camp : cambc::vision::camps()) {
        try {
            if (!is_safe_farmable_camp(camp)) continue;
            if (me.attack_tiles().is_set(camp.position())) {
                float hp = camp.health();
                if (hp < goal_min_hp) {
                    goal_min_hp = hp;
                    goal_camp = camp;
                }
            }
        } catch (...) {}
    }
    if (goal_camp) {
        if (attack_target(*goal_camp)) {
            try_support_abilities();
            maybe_ward();
            return;
        }
    }
}

}  // namespace

// =========================================================================
//  Bot initialization
// =========================================================================
void Bot::init() {
    auto& me = cambc::self();
    build_item = build_for(me.champion_name());
    lane = my_lane();

    auto dm = cambc::draft_memory;
    if (dm.size() >= 16
        && static_cast<char>(dm[0]) == 'M'
        && static_cast<char>(dm[1]) == 'B'
        && static_cast<char>(dm[2]) == '0'
        && static_cast<char>(dm[3]) == '3') {
        num_groups = static_cast<int>(static_cast<uint8_t>(dm[4]));
        if (lane >= 0 && lane < 5) {
            assigned_camp = static_cast<int>(static_cast<uint8_t>(dm[6 + lane]));
        }
        for (int gi = 0; gi < num_groups && static_cast<std::size_t>(16 + gi * 2 + 1) < dm.size(); ++gi) {
            group_altar[gi]   = static_cast<int>(static_cast<uint8_t>(dm[16 + gi * 2]));
            group_altar_d[gi] = static_cast<int>(static_cast<uint8_t>(dm[16 + gi * 2 + 1]));
        }
        draft_parsed = true;
    }

    initialized = true;
}

// =========================================================================
//  Shopping: prioritize stat shards first; only lane 0 buys one altar lock
// =========================================================================
void Bot::buy_items(bool at_spawn) {
    auto& me = cambc::self();
    const bool is_tavi = me.champion_name() == cambc::champion_type::tavi;
    if (at_spawn || !me.is_alive() || is_tavi) {
        try {
            // Buy stat shards with all gold
            const unsigned count = me.gold_count() / build_item.cost();
            if (count && me.can_buy(build_item, count))
                me.buy(build_item, count);
        } catch (...) {}
    }
}

// =========================================================================
//  MAIN TURN
// =========================================================================
void Bot::play_turn() {
    try {
        if (cambc::fuel_remaining() < 2000) {
            cambc::end_turn();
            return;
        }
        if (!initialized) init();

    auto& me = cambc::self();
    lane = my_lane();

    // Read shared memory
    SMem sm = SMem::read();

    // Dead: shop and clear status
    if (!me.is_alive()) {
        uint32_t current_gems = 0;
        try { current_gems = me.gem_count(); } catch (...) {}
        int gem_delta = static_cast<int>(current_gems) - static_cast<int>(my_last_gem_count);
        if (gem_delta != 0) {
            int total = static_cast<int>(sm.team_carried_gems()) + gem_delta;
            if (total < 0) total = 0;
            sm.set_team_carried_gems(static_cast<uint16_t>(total));
            my_last_gem_count = current_gems;
        }
        buy_items(false);
        sm.set_tributing(lane, false);
        sm.clear_chaser_if(lane);
        sm.set_status(lane, STATUS_IDLE);
        sm.write();
        return;
    }

    // Active channels: let them finish
    if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())
        return;

    // Cannot act this turn
    if (!me.can_move() && !me.can_attack() && !me.can_cast()) {
        buy_items(false);
        return;
    }

    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    buy_items(at_spawn);

    // Sync carried gems across all champions in shared memory
    uint32_t current_gems = me.gem_count();
    int gem_delta = static_cast<int>(current_gems) - static_cast<int>(my_last_gem_count);
    if (gem_delta != 0) {
        int total = static_cast<int>(sm.team_carried_gems()) + gem_delta;
        if (total < 0) total = 0;
        if (total > 1023) total = 1023;
        sm.set_team_carried_gems(static_cast<uint16_t>(total));
        my_last_gem_count = current_gems;
    }

    // Total gems across all champions: submitted + carried by all teammates
    uint32_t submitted = cambc::global_submitted_gems(team::ally);
    uint32_t total_team_gems = submitted + sm.team_carried_gems();
    const bool can_win_now = (total_team_gems >= 1000);

    // Update shared memory
    scan_enemies_and_altars(sm);

    // -----------------------------------------------------------------
    //  1. SURVIVAL (Defensive, Exception-Safe)
    // -----------------------------------------------------------------
    if (!at_spawn && me.health() < me.max_health() * 0.35f) {
        bool near_altar = false;
        for (auto a : cambc::map::altars().positions()) {
            if (cheb(me.position(), a) <= cambc::constants::submit_gems_range) {
                near_altar = true;
                break;
            }
        }
        if (!near_altar || me.gem_count() == 0) {
            // Check if any hostile unit (monster or enemy) is within attack range
            bool threat_near = false;
            for (const auto& c : cambc::vision::camps()) {
                try {
                    if (c.health() > 0.0f && cheb(me.position(), c.position()) <= 3) {
                        threat_near = true;
                        break;
                    }
                } catch (...) {}
            }
            if (!threat_near) {
                for (const auto& e : cambc::vision::champions(team::enemy)) {
                    try {
                        if (is_safe_enemy_target(e) && cheb(me.position(), e.position()) <= 4) {
                            threat_near = true;
                            break;
                        }
                    } catch (...) {}
                }
            }

            if (threat_near) {
                // Defensive support abilities / shield
                try_support_abilities();

                // Hit back while retreating if enemy champion in range!
                for (const auto& e : cambc::vision::champions(team::enemy)) {
                    if (attack_target(e)) break;
                }

                // Dash / move toward ally spawn
                auto spawn_tiles = cambc::map::spawn_region(team::ally);
                if (!spawn_tiles.is_empty()) {
                    vec2 home = spawn_tiles.positions()[0];
                    if (cheb(me.position(), home) > 1) {
                        try_dash_toward(home);
                    }
                    move_towards_closest(spawn_tiles);
                }
            } else {
                // Safe distance from threats (> 3 tiles): recall safely
                try { me.recall(); } catch (...) {}
            }
            sm.set_tributing(lane, false);
            sm.clear_chaser_if(lane);
            sm.set_status(lane, STATUS_IDLE);
            sm.write();
            return;
        }
    }

    // -----------------------------------------------------------------
    //  1.5 FINISH ACTIVE CAMP CAPTURE: [Fix #4]
    //  If standing on reward tile waiting 5 rounds, STAY and finish capture!
    //  Or if near an available capture tile (<= 4 tiles), STEP ONTO IT!
    // -----------------------------------------------------------------
    if (!can_win_now) {
        const auto visible = cambc::vision::visible_tiles();
        for (const auto& group : cambc::map::camp_groups()) {
            if (is_group_reward_available(group, visible)) {
                if (group.spawn_tiles().is_set(me.position())) {
                    // Attack any contester in range
                    for (const auto& e : cambc::vision::champions(team::enemy)) {
                        if (attack_target(e)) {
                            try_support_abilities();
                            break;
                        }
                    }
                    maybe_ward();
                    sm.set_status(lane, STATUS_FARMING);
                    sm.write();
                    return;
                }

                // If an ally is already standing on this capture tile, do not crowd it!
                bool ally_there = false;
                for (const auto& a : cambc::vision::champions(team::ally)) {
                    try {
                        if (a != me && group.spawn_tiles().is_set(a.position())) {
                            ally_there = true;
                            break;
                        }
                    } catch (...) {}
                }
                if (ally_there) continue;

                cambc::bitboard valid_spawn = group.spawn_tiles() & ~blocked_tiles() & visible;
                if (!valid_spawn.is_empty()) {
                    vec2 best_t = me.position();
                    int min_d = 999;
                    for (auto t : valid_spawn.positions()) {
                        int d = cheb(me.position(), t);
                        if (d < min_d) { min_d = d; best_t = t; }
                    }
                    if (min_d <= 4) {
                        if (min_d > 1) try_dash_toward(best_t);
                        move_towards_closest(valid_spawn);
                        for (const auto& e : cambc::vision::champions(team::enemy)) {
                            if (attack_target(e)) {
                                try_support_abilities();
                                break;
                            }
                        }
                        if (group.spawn_tiles().is_set(me.position())) {
                            maybe_ward();
                        }
                        sm.set_status(lane, STATUS_FARMING);
                        sm.write();
                        return;
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------
    //  2. TRIBUTE DECISION: [Fix #2]
    //  Bank carried gems immediately unless finishing active local capture
    // -----------------------------------------------------------------
    if (should_tribute(me, total_team_gems)) {
        if (try_tribute(sm, lane)) {
            sm.write();
            return;
        }
    }

    // -----------------------------------------------------------------
    //  2.5 RETURN TO LANE AFTER TRIBUTE CHANNEL FINISHES: [Fix #7]
    //  When tribute channel finishes (sm had tributing flag, now 0 gems):
    //  Clear tributing flag and immediately farm/return rather than idling!
    // -----------------------------------------------------------------
    if (sm.is_tributing(lane) && me.gem_count() == 0) {
        sm.set_tributing(lane, false);
        sm.set_status(lane, STATUS_FARMING);
    }

    // -----------------------------------------------------------------
    //  3. CHASE ENEMY GEM CARRIERS: [Fix #5]
    // -----------------------------------------------------------------
    if (try_chase_enemy(sm, lane)) {
        sm.write();
        return;
    }

    // -----------------------------------------------------------------
    //  4. ATTACK ENEMIES IN RANGE (Focus fire lowest HP enemy!)
    // -----------------------------------------------------------------
    std::optional<cambc::champion> best_enemy;
    float lowest_enemy_hp = 999999.0f;
    for (const auto& enemy : cambc::vision::champions(team::enemy)) {
        try {
            if (!is_safe_enemy_target(enemy)) continue;
            if (me.attack_tiles().is_set(enemy.position())) {
                float hp = enemy.health();
                if (hp < lowest_enemy_hp) {
                    lowest_enemy_hp = hp;
                    best_enemy = enemy;
                }
            }
        } catch (...) {}
    }
    if (best_enemy) {
        if (attack_target(*best_enemy)) {
            try_support_abilities();
            me.set_priority(priority_for(me.champion_name(), false));
            sm.clear_chaser_if(lane);
            sm.set_status(lane, STATUS_CHASING);
            sm.write();
            return;
        }
    }

    // -----------------------------------------------------------------
    //  5. FARM CAMPS & ENEMY BASE RAIDING
    // -----------------------------------------------------------------
    sm.set_tributing(lane, false);
    sm.clear_chaser_if(lane);
    sm.set_status(lane, STATUS_FARMING);

    farm_camps(lane, assigned_camp, sm, can_win_now);

    try_support_abilities();
    maybe_ward();
    me.set_priority(priority_for(me.champion_name(), false));
    sm.write();

    if (cambc::fuel_remaining() < 1000)
        cambc::end_turn();
    } catch (...) {}
}
