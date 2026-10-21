//! zig_diff spec: func_800A25EC -> func_800A25EC.func_800A25EC (38C1C.c).
//! Inputs: an actor in the arena whose position and next-step vectors are edge
//! values (0, +-1, 127, -128, 32767, -32768), so the i16 sums wrap and the
//! tile divisions see negative positions. The step-height stub returns the
//! sentinel -3000 (also as -3000 + 65536: the C tests the short it stored,
//! not the int), the current height (equal), the height 64 or 63 above
//! (the >= bound), 65 above, 1 below, or a random height; the tile-search stub
//! returns null a third of the time; unkA_0 (the 0x70000 test) is zero a third
//! of the time and 1-7 otherwise.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A25EC));

pub const classes = [_][:0]const u8{
    "early-return",
    "no-target",
    "flag-set",
    "step-up",
    "step-short",
    "level",
    "random-height",
};

/// Set by prepare, read by the stubs; the same in the C run and the Zig run.
var ret_mode: u32 = 0;
var stub_py: i16 = 0;
var stub_null: bool = false;

pub const stubs = struct {
    pub fn func_800A6EE8(ctx: *zd.Ctx, pos: anytype, x: anytype, z: anytype, flag: anytype) i32 {
        _ = pos;
        _ = x;
        _ = z;
        _ = flag;
        const py: i32 = stub_py;
        return switch (ret_mode) {
            0 => -3000,
            1 => py,
            2 => py + 64,
            3 => py + 63,
            4 => py + 65,
            5 => py - 1,
            // -3000 + 65536: the C tests the stored short (-3000), not the int.
            6 => -3000 + 65536,
            else => ctx.value(i32),
        };
    }

    pub fn func_800A8D64(ctx: *zd.Ctx, next: anytype, flag: anytype) ?*anyopaque {
        _ = ctx;
        _ = next;
        _ = flag;
        if (stub_null) return null;
        return @ptrCast(&zd.zdiff_arena);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const edges = [_]i16{ 0, 1, -1, 127, -128, 32767, -32768 };
    actor.unk1848.unk10.vx = ctx.pick(i16, &edges);
    actor.unk1848.unk10.vz = ctx.pick(i16, &edges);
    actor.unk0.position.vx = ctx.pick(i16, &edges);
    actor.unk0.position.vz = ctx.pick(i16, &edges);
    actor.unk0.position.vy = ctx.pick(i16, &edges);

    if (ctx.chance(3)) {
        psx.setBits(&actor.unk0, "unkA_0", 0);
    } else {
        psx.setBits(&actor.unk0, "unkA_0", ctx.range(u3, 1, 7));
    }

    ret_mode = ctx.range(u32, 0, 8);
    stub_null = ctx.chance(3);
    stub_py = actor.unk0.position.vy;
    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if (ret_mode == 0 or ret_mode == 6) return 0;
    if (stub_null) return 1;
    if (psx.getBits(&actor.unk0, "unkA_0") != 0) return 2;
    return switch (ret_mode) {
        2, 4 => 3,
        3 => 4,
        1, 5 => 5,
        else => 6,
    };
}
