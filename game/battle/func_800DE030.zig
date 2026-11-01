//! The per-tick recovery and status upkeep of every battle actor
//! (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// `(*(statusEffectTable*)vs_main_statusEffectParams).entries[index].interval`.
fn statusInterval(index: c_int) c_int {
    const table: *v.statusEffectTable = @ptrCast(v.g.vs_main_statusEffectParams());
    return psx.at(&table.entries, index).interval;
}

/// orig: func_800DE030 (src/BATTLE/BATTLE.PRG/74088.c)
/// Advances the battle clock by `elapsed`: limb, HP and MP regeneration, poison and regen ticks, status expiry, then each actor's action candidates.
pub fn func_800DE030(elapsed: c_int) callconv(.c) void {
    const ticks: c_int = (elapsed >> 5) +% (elapsed >> 9);
    const clock = v.g.D_800F58BC().*.?;
    clock.unk18 = psx.conv(u16, @as(c_int, clock.unk18) +% elapsed);

    var actor = v.g.vs_battle_actors()[0];
    while (actor) |a| : (actor = psx.get(a.next)) {
        const stats = psx.get(a.stats).?;
        var statuses: c_int = stats.statuses;

        if (@as(c_int, v.g.D_800F58BC().*.?.unk18) > 2880) {
            var limb: usize = 0;
            while (limb < 6) : (limb += 1) {
                const limbs = &stats.limbs[limb];
                limbs.hp = psx.conv(i16, @as(c_int, limbs.hp) +% 2);
                if (limbs.hp > limbs.maxHp) {
                    limbs.hp = limbs.maxHp;
                }
            }
        }

        if (psx.at(&stats.limbs, psx.getBits(stats, "unk34")).hp >= 2 or psx.at(&stats.limbs, psx.getBits(stats, "unk35")).hp >= 2) {
            statuses &= ~@as(c_int, 0x10);
        }

        var amount: c_int = @divTrunc(elapsed, 240);
        if (amount > 0) {
            stats.currentHP = psx.conv(i16, @as(c_int, stats.currentHP) +% (amount -% 1));
        }
        if (stats.currentHP > stats.maxHP) {
            stats.currentHP = stats.maxHP;
        }

        amount = @divTrunc(elapsed, 180);
        if (amount > 0) {
            stats.currentMP = psx.conv(i16, @as(c_int, stats.currentMP) +% (amount -% 1));
        }
        if (stats.currentMP > stats.maxMP) {
            stats.currentMP = stats.maxMP;
        }

        if (statuses & 0x4000 != 0) {
            const scale: c_int = @as(c_int, stats.maxHP) *% 5;
            stats.currentHP = psx.conv(i16, @as(c_int, stats.currentHP) -%
                @divTrunc(@divTrunc(ticks, statusInterval(14)) *% scale, 100));
            if (stats.currentHP < 0) {
                stats.currentHP = 0;
            }
        }

        if (statuses & 0x20000 != 0) {
            const scale: c_int = @as(c_int, stats.maxHP) *% 5;
            stats.currentHP = psx.conv(i16, @as(c_int, stats.currentHP) +%
                @divTrunc(@divTrunc(ticks, statusInterval(17)) *% scale, 100));
            if (stats.currentHP < 0) {
                stats.currentHP = 0;
            }
        }

        var i: usize = 0;
        while (i < 5) : (i += 1) {
            const mask: c_uint = v.g.D_80069BBC()[i];
            if (@as(c_uint, @bitCast(statuses)) & mask != 0) {
                var duration: c_int = stats.statusTimers[i];
                duration = (duration << 5) -% (duration << 1);
                if (@as(c_int, v.g.D_800F58BC().*.?.unk18) >= duration) {
                    statuses = @bitCast(@as(c_uint, @bitCast(statuses)) & ~v.g.D_80069BBC()[i]);
                }
            }
        }

        stats.statuses = statuses;
        const state = psx.at(v.g.D_800F5878(), a.id).*;
        c.func_800E4F14(@ptrCast(state));
        var row: c_int = 0;
        while (row < 6) : (row += 1) {
            var column: c_int = 0;
            while (column < 4) : (column += 1) {
                _ = c.func_800E4DF8(@ptrCast(state), row, column);
            }
        }
    }
}
