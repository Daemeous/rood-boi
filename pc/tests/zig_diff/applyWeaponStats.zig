//! zig_diff spec: vs_battle_applyWeaponStats -> weapon_stats.applyWeaponStats (146C.c).
//! Inputs: an arena UI weapon (any bytes) and an arena weapon from parts
//! (any bytes: every i8 stat, class and affinity, so sums go negative and
//! wrap the u16 base stats), its damage type 0-3 or 4-7 (past the grip's 4
//! types, into its classes, as the C reads). vs_main_memcpy is a recording
//! stub (arguments and the state at each call).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").applyWeaponStats));

pub const classes = [_][:0]const u8{ "damage-type-0-3", "damage-type-4-7", "negative-strength" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const target = ctx.alloc(v.vs_battle_uiWeapon);
    const source = ctx.alloc(v.vs_battle_weaponIntermediate);
    source.blade.damageType = if (ctx.chance(4)) ctx.range(u8, 4, 7) else ctx.range(u8, 0, 3);
    return .{ target, source };
}

fn strength(s: *v.vs_battle_weaponIntermediate) c_int {
    return @as(c_int, s.blade.strength) + s.grip.strength + s.gems[0].strength + s.gems[1].strength + s.gems[2].strength;
}

pub fn classify(args: Args) usize {
    const s = args[1].?;
    if (s.blade.damageType >= 4) return 1;
    return if (strength(s) < 0) 2 else 0;
}
