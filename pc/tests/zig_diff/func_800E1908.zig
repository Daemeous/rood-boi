//! zig_diff spec: func_800E1908 -> func_800E1908.func_800E1908 (771A8.c).
//! Inputs: a terrainMovementState in the arena with a valid arena actor (the C
//! has no null test), a 64 KB tile-mask table whose address the scratchpad word
//! at 0x1F8003C4 holds, tile x and z over their edges (the index reaches 65280),
//! a tile-map width of 0, 1, 2 or 255, targets within +-2^30 so position - target
//! never overflows int, and angle over its edges. The terrain mode the callee
//! leaves in the scratchpad is 5, 7, or another value in 0..31 (the shift
//! `0xA0 >> mode` is undefined outside that range). func_800E3600 writes the
//! state's angle and the mode word, which the C reads back after the call.
//! The tested value `(0xA0 >> mode) & 1` is structurally non-negative, so no
//! input can give it the sign bit; the signed angle covers the sign-bit case.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E1908));

pub const classes = [_][:0]const u8{ "mode-5", "mode-7", "mode-other" };

/// The mode func_800E3600 leaves in the scratchpad (set by prepare).
var plan_mode: i32 = 0;

fn scratch(comptime T: type, off: usize) *align(1) T {
    return @ptrFromInt(zd.scratchpad + off);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.terrainMovementState);
    const actor = ctx.alloc(v.terrainMovementActor);
    const table = ctx.allocBytes(65536, 16);

    psx.put(&state.actor, actor);
    scratch(u32, 0x3C4).* = psx.addr(table);
    scratch(u8, 0x3DC).* = ctx.pick(u8, &.{ 0, 1, 2, 255 });

    state.tile.p.x = ctx.pick(u8, &.{ 0, 1, 255 });
    state.tile.p.z = ctx.pick(u8, &.{ 0, 1, 255 });
    state.tile.p.y = ctx.int(u8);

    state.targetX = ctx.range(i32, -(1 << 30), 1 << 30);
    state.targetZ = ctx.range(i32, -(1 << 30), 1 << 30);
    state.position[0] = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    state.position[1] = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    state.position[2] = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    state.angle = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });

    // The mode before the call is random; the callee overwrites it with plan_mode.
    scratch(i32, 0x3C8).* = ctx.range(i32, 0, 31);
    plan_mode = if (ctx.chance(2)) (if (ctx.chance(2)) 5 else 7) else ctx.range(i32, 0, 31);

    return .{state};
}

pub fn classify(args: Args) usize {
    _ = args;
    return switch (plan_mode) {
        5 => 0,
        7 => 1,
        else => 2,
    };
}

pub const stubs = struct {
    pub fn func_800E3600(ctx: *zd.Ctx, s: anytype, angle: anytype, flag: anytype) void {
        _ = angle;
        _ = flag;
        const st: *v.terrainMovementState = @ptrCast(@alignCast(s));
        st.angle = ctx.value(i16);
        scratch(i32, 0x3C8).* = plan_mode;
    }
};
