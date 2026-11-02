//! zig_diff spec: func_800C4794 -> func_800C4794.func_800C4794 (5BF94.c).
//! Inputs: the cursor state D_800EB9B8 (an arena block): busy (unk2A) or
//! not; its pick flag (unk38's pad) 0 (the height path) or set (the ground
//! path, unless R2 is held); its mode 1..5 (the scale cases) or another on
//! the ground path only (on the height path no case sets the C's scale: the
//! spec never builds it), its height unit 1..255 (0 divides by zero); its
//! triggers 0..3 arena entries. The stick, held and pressed buttons at their
//! edges; the trigonometry, rsqrt (across 4096) and the collision map's
//! size (the clamp; the map's edge on the cursor's point now and then)
//! from stubs; func_800C085C returns an arena matrix (zero now and then).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C4794));

pub const classes = [_][:0]const u8{ "busy", "height", "ground" };

var matrix: ?*v.MATRIX = null;

pub const stubs = struct {
    pub fn func_800C085C(ctx: *zd.Ctx, flags: anytype, angle: anytype) ?*v.MATRIX {
        _ = ctx;
        _ = flags;
        _ = angle;
        return matrix;
    }
    pub fn vs_battle_mapStickDeadZone(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return if (ctx.value(u8) < 128) 0 else ctx.value(i8);
    }
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return switch (ctx.value(u8) % 4) {
            0 => 0x1000,
            1 => 0x1001,
            2 => 100,
            else => ctx.value(u16),
        };
    }
    pub fn _getCollisionMapDimensions(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        // The map's edge on the cursor's own point now and then (it lands there when its offset is 0).
        const at: *v.SVECTOR = &v.g.D_800EB9B8().*.?.unk0.unk8;
        return switch (ctx.value(u8) % 5) {
            0 => 0x00400040,
            1 => (@as(c_int, @as(u16, @bitCast(at.vx))) >> 3) | (@as(c_int, at.vz >> 3) << 16),
            2 => (@as(c_int, @as(u16, @bitCast(at.vx))) >> 3) | 0x7FFF0000,
            3 => 0x7FFF | (@as(c_int, at.vz >> 3) << 16),
            else => ctx.value(c_int),
        };
    }
    pub fn SquareRoot12(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return if (ctx.value(u8) < 32) 0 else ctx.value(c_int);
    }
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        // The pair (0, 9) or (0, 1) now and then: their difference & 0xFFF is the turn window's edge, 4087, or past it.
        const second = ctx.calls == last_ratan +% 1;
        last_ratan = ctx.calls;
        if (pair) return if (second) (if (ctx.value(u1) == 0) @as(c_int, 9) else 1) else 0;
        return ctx.value(c_int);
    }
};

var last_ratan: u32 = 0xFFFF_FFF0;
var pair = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    matrix = ctx.alloc(v.MATRIX);
    if (ctx.chance(6)) matrix.?.* = std.mem.zeroes(v.MATRIX);
    pair = ctx.chance(4);
    const state = ctx.alloc(v.func_800C56C0_t);
    v.g.D_800EB9B8().* = state;
    state.unk2A = ctx.pick(i16, &.{ 0, 0, 0, 1 });
    const pick = ctx.edge(i16, &.{ 0, 0, 1 });
    state.unk38.vec.pad = pick;
    const held = ctx.edge(i32, &.{ 0, 0x80, 0x1000, 0x4000, 0x8000, 0x2000, 0x5000 });
    v.g.vs_main_buttonsState().* = held;
    const ground = pick != 0 and (held & 0x80) == 0;
    var mode = ctx.edge(u8, &.{ 1, 2, 3, 4, 5, 0, 6, 0x81, 0x84 });
    if (!ground and (mode & 0x7F == 0 or mode & 0x7F > 5)) mode = 1;
    state.unk0.unk0 = mode;
    psx.setBits(&state.unk0.unk4.flags, "unk1", ctx.edge(u8, &.{ 1, 2, 4, 255 }));
    state.unk2E = ctx.range(u8, 0, 3);
    psx.put(&state.unk44, @as(*v.func_800C5798_t, @ptrCast(@alignCast(ctx.allocBytes(4 * @sizeOf(v.func_800C5798_t), 16)))));
    state.unk0.unk8.vy = ctx.pick(i16, &.{ 0, 1, -1, -32768, 100 });
    state.unk0.unk8.vx = ctx.pick(i16, &.{ 0, 0, 8, 256, -8 }) & ~@as(i16, 7);
    state.unk0.unk8.vz = ctx.pick(i16, &.{ 0, 0, 8, 256, -8 }) & ~@as(i16, 7);
    state.unk38.vec.vy = ctx.pick(i16, &.{ 0, 1, -1, -64, 64 });
    if (ctx.random.boolean()) {
        state.unk38.vec.vx = 0;
        state.unk38.vec.vz = 0;
    }
    v.g.vs_main_buttonsPressed().all = ctx.edge(u32, &.{ 0, 0, 0x40, 0x20 });
    return .{ctx.alloc(v.SVECTOR)};
}

pub fn classify(args: Args) usize {
    _ = args;
    const state = v.g.D_800EB9B8().*.?;
    if (state.unk2A != 0) return 0;
    if (state.unk38.vec.pad == 0 or (v.g.vs_main_buttonsState().* & 0x80) != 0) return 1;
    return 2;
}
