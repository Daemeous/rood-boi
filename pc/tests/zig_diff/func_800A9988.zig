//! zig_diff spec: func_800A9988 -> func_800A9988.func_800A9988 (40564.c).
//! Inputs: an actor index 0-16 whose table slot is null about one time in six,
//! a random actor (position, in-range or extreme), and an SVECTOR whose
//! position matches the actor's on one axis, both axes, or neither. The pad,
//! vy and vx values are edges and random values, so the 0x8000 tests fire.
//! arg2 and arg3 are 0, -1 or other, so the both-zero branch and the -1
//! stores both run. The 16 D_800F45E0 slots are null or arena objects whose
//! unk5C/unk5E match vx/128 about half the time, with unk1E on both sides of
//! 128 and 0xBB8, so scanFloor returns zero and negative values. scanFloor
//! never returns a positive value (it clamps any positive result to 0), so the
//! `!= 0` test only ever sees zero or negative values; that gap is waived below.
//! The callee stubs write the actor fields the C reads back, and the
//! GTE/ratan2 calls return random values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9988));

pub const classes = [_][:0]const u8{
    "null-actor",
    "first-pad-8000",
    "first-pad-other",
    "equal-pad-8000",
    "equal-pad-other",
    "ratan-vy-8000",
    "ratan-vy-other",
};

fn u16Of(x: i16) u16 {
    return @bitCast(x);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const tab45e0 = psx.global([16]?*v.D_800F45E0_t, "D_800F45E0");

    const actor = ctx.alloc(v.D_800F4538_t);
    const sv = ctx.alloc(v.SVECTOR);

    actor.unk0.position.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    actor.unk0.position.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });

    const idx: c_int = ctx.range(c_int, 0, 16);
    table[@intCast(idx)] = if (ctx.chance(6)) null else actor;

    sv.vx = ctx.pick(i16, &.{ 0, 1, -1, 127, 128, 255, -128, 32767, -32768 });
    sv.vz = ctx.pick(i16, &.{ 0, 1, -1, 127, 128, 255, -128, 32767, -32768 });
    const mode = ctx.range(u8, 0, 3);
    if (mode == 1 or mode == 2) sv.vx = actor.unk0.position.vx;
    if (mode == 1 or mode == 3) sv.vz = actor.unk0.position.vz;
    sv.vy = ctx.pick(i16, &.{ -32768, 0, 1, -1, 32767 });
    sv.pad = ctx.pick(i16, &.{ -32768, 0, 1, -1 });

    const tx = @divTrunc(sv.vx, 128);
    for (0..16) |i| {
        if (ctx.chance(2)) {
            tab45e0[i] = null;
            continue;
        }
        const ent = ctx.alloc(v.D_800F45E0_t);
        if (tx >= 0 and tx < 256 and ctx.chance(2)) {
            ent.unk5C = @intCast(tx);
            ent.unk5E = @intCast(tx);
        } else {
            ent.unk5C = ctx.int(u8);
            ent.unk5E = ctx.int(u8);
        }
        ent.unk1A = ctx.pick(i16, &.{ 0xFE, 0, 5 });
        ent.unk1E = ctx.pick(i16, &.{ 128, 127, 0, -1, 3000, -32768, 0xBB8 });
        psx.setBits(ent, "unk8_0", if (ctx.chance(4)) 1 else 0);
        psx.setBits(ent, "unk9_0", if (ctx.chance(4)) 1 else 0);
        tab45e0[i] = ent;
    }

    const arg2: c_int = ctx.pick(c_int, &.{ 0, -1, 3 });
    const arg3: c_int = ctx.pick(c_int, &.{ 0, -1, 5 });
    return .{ idx, sv, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const actor = table[@intCast(args[0])] orelse return 0;
    const sv = args[1].?;
    if (args[2] == 0 and args[3] == 0) return if (u16Of(sv.pad) == 0x8000) 1 else 2;
    const eq = sv.vx == actor.unk0.position.vx and sv.vz == actor.unk0.position.vz;
    if (eq) return if (u16Of(sv.pad) == 0x8000) 3 else 4;
    return if (u16Of(sv.vy) == 0x8000) 5 else 6;
}

pub const stubs = struct {
    pub fn func_800A9D90(ctx: *zd.Ctx, arg0: c_int, arg1: anytype, arg2: c_int) void {
        _ = arg1;
        _ = arg2;
        if (ctx.value(u1) == 1) {
            if (v.g.D_800F4538()[@intCast(arg0)]) |a| a.unk0.position.vx = ctx.value(i16);
        }
    }

    pub fn func_800A9EB4(ctx: *zd.Ctx, arg0: c_int, angle: c_short, arg2: c_int) void {
        _ = angle;
        _ = arg2;
        if (v.g.D_800F4538()[@intCast(arg0)]) |a| {
            if (ctx.value(u1) == 1) a.unk5C2 = ctx.value(u16);
            if (ctx.value(u1) == 1) a.unk5C0 = ctx.value(u16);
        }
    }
};
