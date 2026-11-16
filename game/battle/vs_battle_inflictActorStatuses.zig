//! Applies status bits to a battle actor: status timers, stat drops and boosts,
//! weapon, shield and armor strength, and the element and status changes
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

fn minusTenth(x: u16) u16 {
    const t: c_int = x;
    return psx.conv(u16, t -% @divTrunc(t, 10));
}

fn plusTenth(x: u16) u16 {
    const t: c_int = x;
    return psx.conv(u16, t +% @divTrunc(t, 10));
}

/// orig: vs_battle_inflictActorStatuses (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds the status bits `arg0` to an actor and applies what each one sets: timers, stat changes, gear strength and the element changes.
pub fn vs_battle_inflictActorStatuses(arg0: c_int, arg1: ?*v.vs_battle_actor2) callconv(.c) void {
    const actor: *v.vs_battle_actor2 = arg1.?;

    actor.statuses |= arg0;

    if ((arg0 & 0x7E0) != 0) {
        actor.statusTimers[0] = v.g.vs_battle_statusParams5Duration().*;
    }

    if ((arg0 & 0x810) != 0) {
        actor.statusTimers[1] = v.g.vs_battle_statusParams11Duration().*;
    }

    if ((arg0 & 0x1FF80000) != 0) {
        actor.statusTimers[2] = v.g.vs_battle_statusParams19Duration().*;
    }

    if ((arg0 & 0x2000) != 0) {
        actor.statusTimers[3] = v.g.vs_battle_statusParams13Duration().*;
    }

    if ((arg0 & 0x8000) != 0) {
        actor.statusTimers[4] = v.g.vs_battle_statusParams13Duration().*;
    }

    if ((arg0 & 0x4000) != 0) {
        actor.statusTimers[5] = v.g.vs_battle_statusParams14TickInterval().*;
    } else if ((arg0 & 0x20000) != 0) {
        actor.statusTimers[5] = v.g.vs_battle_statusParams17TickInterval().*;
    }

    if ((arg0 & 0x20) != 0) {
        actor.strength = minusTenth(actor.totalStrength);
    }

    if ((arg0 & 0x40) != 0) {
        actor.strength = plusTenth(actor.totalStrength);
    }

    if ((arg0 & 0x80) != 0) {
        actor.intelligence = minusTenth(actor.totalIntelligence);
    }

    if ((arg0 & 0x100) != 0) {
        actor.intelligence = plusTenth(actor.totalIntelligence);
    }

    if ((arg0 & 0x200) != 0) {
        actor.agility = minusTenth(actor.totalAgility);
    }
    if ((arg0 & 0x400) != 0) {
        actor.agility = plusTenth(actor.totalAgility);
    }

    if ((arg0 & 0x10000) != 0) {
        actor.strength = minusTenth(actor.totalStrength);
        actor.intelligence = minusTenth(actor.totalIntelligence);
        actor.agility = minusTenth(actor.totalAgility);
    }

    if ((arg0 & 0x8810) != 0) {
        if ((actor.statuses & 0x8010) != 0) {
            actor.unk31 = actor.unk2E >> 1;
            actor.unk33 = actor.unk2F >> 1;
        } else if ((actor.statuses & 0x800) != 0) {
            actor.unk31 = psx.conv(u8, @as(c_int, actor.unk2E) +% (@as(c_int, actor.unk2E) >> 2));
            actor.unk33 = psx.conv(u8, @as(c_int, actor.unk2F) +% (@as(c_int, actor.unk2F) >> 2));
        }

        c.vs_battle_aiSetActorVerticalMovementSteps(@as(c_int, actor.unk957), @as(c_int, actor.unk31), @as(c_int, actor.unk33));
    }

    if ((arg0 & 0x80000) != 0) {
        var var_a3: c_int = @divTrunc((@as(c_int, actor.intelligence) +% @as(c_int, actor.weapon.currentInt)) *% 7, 100);
        if (var_a3 < 0) {
            var_a3 = 0;
        }

        if (actor.weapon.blade.id != 0) {
            actor.weapon.currentStr = psx.conv(i16, @as(c_int, actor.weapon.currentStr) -% var_a3);
        }

        if (actor.shield.base.id != 0) {
            actor.shield.currentStr = psx.conv(i16, @as(c_int, actor.shield.currentStr) -% var_a3);
        }

        var i: usize = 0;
        while (i < 6) : (i += 1) {
            if (actor.limbs[i].armor.armor.id != 0) {
                actor.limbs[i].armor.currentStr = psx.conv(i16, @as(c_int, actor.limbs[i].armor.currentStr) -% var_a3);
            }
        }
    }

    if ((arg0 & 0x100000) != 0) {
        var var_a3: c_int = @divTrunc((@as(c_int, actor.intelligence) +% @as(c_int, actor.weapon.currentInt)) *% 7, 100);
        if (var_a3 < 0) {
            var_a3 = 0;
        }

        if (actor.weapon.blade.id != 0) {
            actor.weapon.currentStr = psx.conv(i16, @as(c_int, actor.weapon.currentStr) +% var_a3);
        }

        if (actor.shield.base.id != 0) {
            actor.shield.currentStr = psx.conv(i16, @as(c_int, actor.shield.currentStr) +% var_a3);
        }

        var i: usize = 0;
        while (i < 6) : (i += 1) {
            if (actor.limbs[i].armor.armor.id != 0) {
                actor.limbs[i].armor.currentStr = psx.conv(i16, @as(c_int, actor.limbs[i].armor.currentStr) +% var_a3);
            }
        }
    }

    if ((arg0 & 0x20000000) != 0) {
        // *(int*)&unk954 |= 0x20000: a 4-byte read-modify-write at unk954's address.
        const unk954: *align(1) i32 = @as(*align(1) i32, @ptrCast(&actor.unk954));
        unk954.* |= 0x20000;
    }

    const bits: c_uint = @bitCast(arg0);
    const temp_a0 = bits & 0x01E00000;

    if (temp_a0 != 0) {
        var var_a3: c_int = @divTrunc(@as(c_int, actor.intelligence) +% @as(c_int, actor.weapon.currentInt), 2);
        if (var_a3 < 0) {
            var_a3 = 0;
        }

        switch (temp_a0) {
            0x200000 => c.vs_battle_applyWeaponElementEnchantment(@ptrCast(actor), 1, 3, var_a3),
            0x400000 => c.vs_battle_applyWeaponElementEnchantment(@ptrCast(actor), 2, 4, var_a3),
            0x800000 => c.vs_battle_applyWeaponElementEnchantment(@ptrCast(actor), 3, 1, var_a3),
            0x1000000 => c.vs_battle_applyWeaponElementEnchantment(@ptrCast(actor), 4, 2, var_a3),
            else => {},
        }
    }

    const temp_b0 = bits & 0x1E000000;

    if (temp_b0 != 0) {
        var var_a3: c_int = @divTrunc(@as(c_int, actor.intelligence) +% @as(c_int, actor.weapon.currentInt), 2);
        if (var_a3 < 0) {
            var_a3 = 0;
        }

        switch (temp_b0) {
            0x2000000 => c.vs_battle_applyArmorElementEnchantment(@ptrCast(actor), 1, 3, var_a3),
            0x4000000 => c.vs_battle_applyArmorElementEnchantment(@ptrCast(actor), 2, 4, var_a3),
            0x8000000 => c.vs_battle_applyArmorElementEnchantment(@ptrCast(actor), 3, 1, var_a3),
            0x10000000 => c.vs_battle_applyArmorElementEnchantment(@ptrCast(actor), 4, 2, var_a3),
            else => {},
        }
    }
}
