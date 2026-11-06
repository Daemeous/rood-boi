//! zig_diff spec: func_8007138C -> start_cast.startCast (146C.c).
//! Inputs: the player's statuses (bit 0x8000 or not); D_800F19CC's current
//! slot: an action index at the C's edges (plain or not: unk2_0 as the harness
//! fills vs_main_actions; 0x8C/0x8D, 0xB7/0xB8, 0xDF/0xE0; below 256), the
//! caster 0..15 (its actor always present), 0..16 hits (the request holds 16
//! targets) on actors 0..3 (so targets repeat) with unk40 0, 4 or 1 and limb
//! -1 or 0..5; actors 0..15 present or not with unk40 0..3, sharing two stats
//! blocks whose armour indices (unkC) stay in range; the information setting.
//! Stubs copy what the C hands over on its stack: the request (all 0xE8 bytes:
//! memset first), the sound position (its pad reaches the GTE: zeroed in both), the
//! object data's set fields (the rest is uninitialised in the C, 0 in the
//! port); func_800A1108 fills the caster's position.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startCast));

pub const classes = [_][:0]const u8{ "released", "plain", "art", "ability", "other" };

pub const stubs = struct {
    pub fn func_800A1108(ctx: *zd.Ctx, actor: anytype, out: anytype) c_int {
        _ = actor;
        const p: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        p.unk0.unk4.vx = ctx.value(i16);
        p.unk0.unk4.vy = ctx.value(i16);
        p.unk0.unk4.vz = ctx.value(i16);
        return ctx.value(c_int);
    }
    pub fn vs_main_panSfx(ctx: *zd.Ctx, a: anytype, b: anytype, vec: anytype) void {
        _ = a;
        _ = b;
        const seen = ctx.alloc(v.SVECTOR);
        const p: *const v.SVECTOR = @ptrCast(@alignCast(vec.?));
        seen.vx = p.vx;
        seen.vy = p.vy;
        seen.vz = p.vz;
        // vs_main_panSfx loads the whole word into the GTE: the pad must be defined (0 in the C under ROOD_PC).
        seen.pad = p.pad;
    }
    pub fn func_800CF0E8(ctx: *zd.Ctx, request: anytype, mode: anytype, unk2: anytype) c_int {
        _ = mode;
        _ = unk2;
        const seen = ctx.alloc(v.func_800CF0E8_t);
        seen.* = @as(*const v.func_800CF0E8_t, @ptrCast(@alignCast(request.?))).*;
        return ctx.value(c_int);
    }
    pub fn vs_battle_populateDataSlot(ctx: *zd.Ctx, data: anytype) c_int {
        const p: *const v.vs_battle_objectData = @ptrCast(@alignCast(data.?));
        const seen = ctx.alloc(v.vs_battle_objectData);
        seen.dataType = p.dataType;
        seen.index = p.index;
        seen.modelId = p.modelId;
        seen.actorId = p.actorId;
        seen.material = p.material;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const blocks = [2]*v.vs_battle_actor2{ ctx.alloc(v.vs_battle_actor2), ctx.alloc(v.vs_battle_actor2) };
    const player = ctx.alloc(v.vs_battle_actor);
    psx.put(&player.stats, blocks[0]);
    v.g.vs_battle_characterState().* = player;
    for (0..16) |k| {
        if (ctx.chance(3)) {
            v.g.vs_battle_actors()[k] = null;
            continue;
        }
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.unk40 = ctx.range(i32, 0, 3);
        actor.unkC = @bitCast(@as(u32, ctx.range(u16, 0, 3)) << 16 | ctx.range(u16, 0, 5));
        psx.put(&actor.stats, blocks[ctx.range(usize, 0, 1)]);
        v.g.vs_battle_actors()[k] = actor;
    }
    const state = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = state;
    const action = &state.unk854[state.unk0 & 3];
    action.actionIndex = ctx.edge(u16, &.{ 0, 0x8C, 0x8D, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xDF, 0xE0, 0xFF, ctx.range(u16, 0, 255) });
    action.unk4.unk0.targetActor = ctx.edge(u8, &.{ 0, 0, 1, 2, ctx.range(u8, 0, 15) });
    if (v.g.vs_battle_actors()[action.unk4.unk0.targetActor] == null) {
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.unkC = @bitCast(@as(u32, ctx.range(u16, 0, 3)) << 16 | ctx.range(u16, 0, 5));
        psx.put(&actor.stats, blocks[1]);
        v.g.vs_battle_actors()[action.unk4.unk0.targetActor] = actor;
    }
    action.unk4A = ctx.edge(u16, &.{ 0, 1, 2, 3, 4, 6, 16 });
    for (&action.unk4C) |*hit| {
        hit.unk40 = ctx.edge(i32, &.{ 0, 0, 0, 4, 1 });
        hit.unk0.targetActor = ctx.range(u8, 0, 3);
        hit.unk0.targetLimb = ctx.edge(i8, &.{ -1, 0, 1, 5 });
    }
    for (0..4) |k| if (v.g.vs_battle_actors()[k] == null) {
        // the hits' targets (0..3) are always present: the C reads their stats
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.unk40 = ctx.range(i32, 0, 3);
        psx.put(&actor.stats, blocks[0]);
        v.g.vs_battle_actors()[k] = actor;
    };
    v.g.vs_main_settings().information = ctx.edge(u8, &.{ 0, 1 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const state = v.g.D_800F19CC().*.?;
    const action = &state.unk854[state.unk0 & 3];
    if (psx.getBits(&v.g.actions()[action.actionIndex], "unk2_0") == 0) return 0;
    if (action.actionIndex < 0x8D) return 1;
    if (action.actionIndex < 0xB8) return 2;
    if (action.actionIndex < 0xE0) return 3;
    return 4;
}
