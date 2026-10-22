// behaviors/combat.cpp
#include "combat.h"
#include <cambc.h>
#include <iostream>
#include <optional>
#include <vector>
#include <algorithm>
#include "../navigation/navigation.h"
#include "../core/memory.h"

namespace Combat {

bool try_attack_enemy_champions() {
    try {
        auto& me = cambc::self();
        if (!me.can_attack()) return false;

        auto atk_tiles = me.attack_tiles();
        std::optional<cambc::champion> best_enemy = std::nullopt;
        float min_hp = 1e9f;

        auto focus = Memory::get_current_focus_target();

        // 1. Focus fire priority: Hit the squad's shared focus target if within our attack tiles!
        if (focus.valid) {
            for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
                if (enemy.is_alive() && enemy.is_visible() && enemy.position() == focus.pos) {
                    if (atk_tiles.is_set(enemy.position())) {
                        try {
                            me.attack(enemy);
                            return true;
                        } catch (...) {}
                    }
                }
            }
        }

        // 2. Second priority: Hit the lowest HP visible enemy within attack range
        for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
            if (enemy.is_alive() && enemy.is_visible()) {
                if (atk_tiles.is_set(enemy.position())) {
                    if (enemy.health() < min_hp) {
                        min_hp = enemy.health();
                        best_enemy = enemy;
                    }
                }
            }
        }

        if (best_enemy.has_value()) {
            try {
                me.attack(*best_enemy);
                // Broadcast focus target so allies concentrate fire!
                Memory::report_enemy_focus_target(best_enemy->position(), best_enemy->health());
                return true;
            } catch (...) {}
        }
    } catch (...) {}
    return false;
}

void try_combat_abilities() {
    try {
        auto& me = cambc::self();
        if (!me.can_cast()) return;

        for (const auto& ab : me.abilities()) {
            if (!ab.is_ready()) continue;

            // 1. Attack Reset (Issac Slot 0, Varg Slot 0, Vendetta Slot 0, Marcella Slot 0, Vesper Vale Slot 0)
            if (ab.is_attack_reset()) {
                if (!me.can_attack()) {
                    try {
                        if (ab.use()) {
                            // After resetting attack, attack immediately if an enemy or camp is in range!
                            if (!try_attack_enemy_champions()) {
                                for (const auto& camp : cambc::vision::camps()) {
                                    if (camp.is_visible() && me.attack_tiles().is_set(camp.position()) && camp.health() > 0) {
                                        try { me.attack(camp); break; } catch (...) {}
                                    }
                                }
                            }
                            return;
                        }
                    } catch (...) {}
                }
            }

            // 2. Self Shield (Varg Slot 1, Heart of Ruin Slot 0)
            if (ab.is_shield()) {
                try {
                    if (ab.use()) return;
                } catch (...) {}
            }

            // 3. Spells with cast targets (CC / damage)
            bool is_attack_spell = ab.is_damage() || ab.is_aoe_damage() || ab.is_cc();
            if (is_attack_spell) {
                // Focus target first
                auto focus = Memory::get_current_focus_target();
                if (focus.valid) {
                    for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
                        try {
                            if (enemy.is_alive() && enemy.is_visible() && enemy.position() == focus.pos) {
                                if (me.position().chebyshev_distance(enemy.position()) <= 4) {
                                    if (ab.is_aoe_damage()) {
                                        if (ab.use(enemy.position())) return;
                                    } else {
                                        if (ab.use(enemy)) return;
                                    }
                                }
                            }
                        } catch (...) {}
                    }
                }

                // Check visible enemy champions sorted by lowest HP
                std::vector<cambc::champion> enemies;
                for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
                    if (enemy.is_alive() && enemy.is_visible()) enemies.push_back(enemy);
                }
                std::sort(enemies.begin(), enemies.end(), [](const auto& a, const auto& b) {
                    return a.health() < b.health();
                });

                for (const auto& enemy : enemies) {
                    try {
                        if (me.position().chebyshev_distance(enemy.position()) <= 4) {
                            if (ab.is_aoe_damage()) {
                                if (ab.use(enemy.position())) {
                                    Memory::report_enemy_focus_target(enemy.position(), enemy.health());
                                    return;
                                }
                            } else {
                                if (ab.use(enemy)) {
                                    Memory::report_enemy_focus_target(enemy.position(), enemy.health());
                                    return;
                                }
                            }
                        }
                    } catch (...) {}
                }

                // Check visible neutral monsters (skip dormant high-strength monsters)
                for (const auto& camp : cambc::vision::camps()) {
                    try {
                        if (camp.is_visible() && camp.is_alive() && camp.health() > 0) {
                            if (camp.strength() > 0 && !camp.is_awake()) {
                                continue;
                            }
                            if (me.position().chebyshev_distance(camp.position()) <= 4) {
                                if (ab.is_aoe_damage()) {
                                    if (ab.use(camp.position())) return;
                                } else {
                                    if (ab.use(camp)) return;
                                }
                            }
                        }
                    } catch (...) {}
                }
            }

            // 4. Combat Dash: Close in on enemies to strike (Ren, Vendetta, Heart of Ruin, Vesper Vale)
            if (ab.is_dash()) {
                for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
                    if (enemy.is_alive() && enemy.is_visible()) {
                        int dist = me.position().chebyshev_distance(enemy.position());
                        if (dist >= 2 && dist <= 3) {
                            cambc::bitboard cands;
                            cands.clear();
                            cands.draw_chebyshev(me.position(), 2);
                            cands &= ~Navigation::blocked_tiles();
                            for (auto tile : cands.positions()) {
                                if (tile.chebyshev_distance(enemy.position()) <= 1) {
                                    try {
                                        if (ab.use(tile)) {
                                            try_attack_enemy_champions();
                                            return;
                                        }
                                    } catch (...) {}
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {}
}

bool try_attack_altar_lock() {
    try {
        auto& me = cambc::self();
        auto attack_tiles = me.attack_tiles();
        for (const auto& lock : cambc::vision::altar_locks(cambc::team::enemy)) {
            try {
                if (attack_tiles.is_set(lock.position())) {
                    if (me.can_attack()) {
                        try { me.attack(lock); } catch (...) {}
                    }
                    return true; // Commit to destroying lock, do not walk away!
                }
            } catch (...) {}
        }
    } catch (...) {}
    return false;
}

bool try_chase_visible_enemy() {
    try {
        auto& me = cambc::self();
        if (!me.can_move()) return false;
        if (me.health() < me.max_health() * 0.45f) return false;
        if (me.gem_count() > 10) return false;

        // Ranged Champion Kiting (Vesper Vale, Stellar):
        // If an enemy champion gets within distance 1 or 2, kite backward to maintain range 3!
        if (me.attack_range() >= 3) {
            for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
                if (enemy.is_alive() && enemy.is_visible()) {
                    int d = static_cast<int>(me.position().chebyshev_distance(enemy.position()));
                    if (d <= 2) {
                        if (Navigation::move_away_from(enemy.position())) {
                            try_attack_enemy_champions();
                            try_combat_abilities();
                            return true;
                        }
                    }
                }
            }
        }

        cambc::vec2 target_pos{-1, -1};
        int closest_dist = 999;
        
        // Only chase enemies that are either in immediate brawl range (<= 3)
        // OR high-value gem carriers (<= 6 distance and carrying >= 5 gems)
        for (const auto& enemy : cambc::vision::champions(cambc::team::enemy)) {
            if (enemy.is_alive() && enemy.is_visible()) {
                int d = me.position().chebyshev_distance(enemy.position());
                if (d <= 3 || (d <= 6 && enemy.gem_count() >= 5)) {
                    if (d < closest_dist) {
                        closest_dist = d;
                        target_pos = enemy.position();
                    }
                }
            }
        }

        if (closest_dist <= 6 && target_pos.x >= 0) {
            // For ranged champions: if already in attack range (<= 3), don't walk into melee!
            if (me.attack_range() >= 3 && closest_dist <= me.attack_range()) {
                try_attack_enemy_champions();
                try_combat_abilities();
                return true;
            }

            cambc::bitboard target_tiles;
            target_tiles.clear();
            target_tiles.draw_chebyshev(target_pos, me.attack_range());
            target_tiles &= ~cambc::map::altars();

            if (!target_tiles.is_empty()) {
                if (Navigation::move_towards_closest(target_tiles)) {
                    try_attack_enemy_champions();
                    try_combat_abilities();
                    return true;
                }
            }
        }
    } catch (...) {}
    return false;
}

} // namespace Combat
