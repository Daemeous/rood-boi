//! zig_diff spec: vs_battle_equipShield -> equip_shield.equipShield (146C.c).
//! Inputs: the global character state points at an arena actor whose stats
//! block is always an arena block (the C dereferences it unguarded, as the
//! game does), and the shield argument is null (1 in 8) or an arena block.
//! The two callees write their outputs: applyShield writes the destination
//! shield's index and DP, nop0 writes the stats' maxHP, so a port that reads
//! either back before the call differs. The two func_ calls take no arguments
//! and are recording stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").equipShield));

pub const classes = [_][:0]const u8{ "shield-null", "shield-set" };

pub const stubs = struct {
    pub fn vs_battle_applyShield(ctx: *zd.Ctx, dst: anytype, shield: anytype) void {
        _ = shield;
        const s: ?*v.vs_battle_uiShield = @ptrCast(@alignCast(dst));
        if (s) |d| {
            d.index = ctx.value(u8);
            d.currentDp = ctx.value(u16);
        }
    }

    pub fn vs_battle_nop0(ctx: *zd.Ctx, p: anytype) void {
        const s: ?*v.vs_battle_actor2 = @ptrCast(@alignCast(p));
        if (s) |a| a.maxHP = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    psx.put(&actor.stats, stats);
    v.g.vs_battle_characterState().* = actor;
    const shield: ?*v.vs_main_inventoryShield = if (ctx.chance(8)) null else ctx.alloc(v.vs_main_inventoryShield);
    return .{shield};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
