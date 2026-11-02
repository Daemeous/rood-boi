//! zig_diff spec: func_80070F28 -> commit_action.commitAction (146C.c).
//! Inputs: the kind 0, 1, 2 or another; a pending menu choice (unk298C 0xFF)
//! or not; the action's target 0 or another (0..15: the actor loop compares
//! it), its index at the C's edges (1, 0xB7/0xB8, 0xDF/0xE0, 54/55, the
//! heal-count cases, below 256: vs_main_actions is indexed by it); the
//! ability menu state null (the ROOD_PC guard reads low RAM) or an object with
//! unk2 7, 8 or another; the first hit's unk40 0 or not and its target's unk27
//! 0x80 or not; the player's weapon category 0..10 with its attack count at
//! 0xFFFE/0xFFFF or below; the heal count at 0x3E7/0x3E8 or below;
//! func_80077F70 at 0x1C0/0x1C1 or another.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").commitAction));

pub const classes = [_][:0]const u8{ "dropped-targeted", "dropped-plain", "dropped-other", "player", "enemy" };

pub const stubs = struct {
    /// Settles the pending menu choice (unk298C) now and then, as the menu would.
    pub fn func_8006EF10(ctx: *zd.Ctx) void {
        if (ctx.value(u8) % 4 == 0) v.g.D_800F19CC().*.?.unk298C ^= 0xFF;
    }
    pub fn func_80077F70(ctx: *zd.Ctx) c_int {
        return ctx.pick(c_int, &.{ 0x1C0, 0x1C1, 0, 0x7FFF_FFFF });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = state;
    const kind = ctx.edge(c_int, &.{ 0, 0, 1, 1, 2, 2, 3, -1 });
    state.unk298C = if (ctx.chance(2)) 0xFF else ctx.int(u8);
    state.unk8.unk4.unk0.targetActor = if (ctx.chance(2)) 0 else ctx.range(u8, 1, 15);
    state.unk8.actionIndex = ctx.edge(u16, &.{ 0, 1, 54, 55, 0x4D, 0x60, 0xB7, 0xB8, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, ctx.range(u16, 0, 255) });
    if (ctx.chance(4)) {
        psx.global(?*v.D_800F18EC_t, "D_800F18EC").* = null;
    } else {
        const ability = ctx.alloc(v.D_800F18EC_t);
        ability.unk2 = ctx.edge(i16, &.{ 7, 8, 8, 0, -1 });
        psx.global(?*v.D_800F18EC_t, "D_800F18EC").* = ability;
    }
    const hit = &state.unk8.unk4C[0];
    hit.unk40 = if (ctx.chance(3)) ctx.int(i32) else 0;
    hit.unk0.targetActor = ctx.range(u8, 0, 15);
    const target = ctx.alloc(v.vs_battle_actor);
    target.unk27 = if (ctx.chance(3)) 0x80 else ctx.int(u8);
    v.g.vs_battle_actors()[hit.unk0.targetActor] = target;
    const player = ctx.alloc(v.vs_battle_actor);
    player.equippedWeaponCategory = ctx.range(u8, 0, 10);
    v.g.vs_battle_characterState().* = player;
    const score = v.g.scoreData();
    const slot: usize = if (player.equippedWeaponCategory == 0xA) 0 else player.equippedWeaponCategory;
    score.weaponAttacks[slot] = ctx.pick(u16, &.{ 0xFFFD, 0xFFFE, 0xFFFF });
    score.healCount = ctx.pick(u16, &.{ 0x3E6, 0x3E7, 0x3E8, 0xFFFF });
    return .{kind};
}

pub fn classify(args: Args) usize {
    const state = v.g.D_800F19CC().*.?;
    if (args[0] != 0 and state.unk298C == 0xFF) {
        if (state.unk8.unk4.unk0.targetActor != 0) return 0;
        return if (zd_unk2_0(state.unk8.actionIndex) == 0) 1 else 2;
    }
    return if (state.unk8.unk4.unk0.targetActor == 0) 3 else 4;
}

fn zd_unk2_0(index: u16) u1 {
    return psx.getBits(&v.g.actions()[index], "unk2_0");
}
