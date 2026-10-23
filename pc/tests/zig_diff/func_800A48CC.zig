//! zig_diff spec: func_800A48CC -> func_800A48CC.func_800A48CC (3A1A0.c).
//! Inputs: a D_800F4538 entry (index 0..16, both ends favoured) pointing at an
//! arena actor whose lock (unk5AC_0), busy (unkA_3) and stepping (unkA_0)
//! fields are each zero or nonzero, and a distance from zero through negative
//! and extreme values to the `distance / 4096 == unk5B9` edge, which prepare
//! makes common by setting unk5B9 from the distance. The callee stubs write the
//! actor's unk11 and unkA_0, so a store moved across a call shows in the state.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A48CC));

pub const classes = [_][:0]const u8{ "locked", "busy", "stepping", "stop", "move-equal", "move-below", "move-above" };

pub const stubs = struct {
    pub fn func_800A0204(ctx: *zd.Ctx, index: anytype, a: anytype, b: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = d;
        const actor = v.g.D_800F4538()[@intCast(index)].?;
        actor.unk0.unk11 = ctx.value(u8);
        psx.setBits(&actor.unk0, "unkA_0", ctx.value(u3));
        return ctx.value(c_int);
    }

    pub fn func_800B64A8(ctx: *zd.Ctx, index: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        const actor = v.g.D_800F4538()[@intCast(index)].?;
        actor.unk0.unk11 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const index: c_int = if (ctx.chance(4)) (if (ctx.chance(2)) 0 else 16) else ctx.range(c_int, 0, 16);
    v.g.D_800F4538()[@intCast(index)] = actor;

    psx.setBits(actor, "unk5AC_0", if (ctx.chance(5)) ctx.range(u2, 1, 3) else 0);
    psx.setBits(&actor.unk0, "unkA_3", if (ctx.chance(5)) ctx.range(u2, 1, 3) else 0);
    psx.setBits(&actor.unk0, "unkA_0", if (ctx.chance(5)) ctx.range(u3, 1, 7) else 0);

    var distance: c_int = switch (ctx.range(u8, 0, 3)) {
        0 => ctx.pick(c_int, &.{ 0, 1, -1, 4095, 4096, -4096, 0x7FFF_FFFF, -0x8000_0000 }),
        1 => ctx.range(c_int, -16384, 16384),
        else => ctx.range(c_int, 0, 256 * 4096 - 1),
    };
    if (ctx.chance(6)) distance = 0;
    // Make `distance / 4096 == unk5B9` common, as the C's >= test sees it.
    if (distance >= 0 and distance < 256 * 4096 and ctx.chance(2)) {
        actor.unk5B9 = @intCast(@divTrunc(distance, 4096));
    }

    const direction: c_int = ctx.pick(c_int, &.{ 0, 1, -1 });
    return .{ index, direction, distance };
}

pub fn classify(args: Args) usize {
    const actor = v.g.D_800F4538()[@intCast(args[0])].?;
    const distance = args[2];
    if (psx.getBits(actor, "unk5AC_0") != 0) return 0;
    if (psx.getBits(&actor.unk0, "unkA_3") != 0) return 1;
    if (psx.getBits(&actor.unk0, "unkA_0") != 0) return 2;
    if (distance == 0) return 3;
    const q = @divTrunc(distance, 4096);
    const b: c_int = @as(c_int, actor.unk5B9);
    if (q == b) return 4;
    return if (q < b) 5 else 6;
}
