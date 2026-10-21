//! zig_diff spec: func_800A208C -> start_jump.startJump (38C1C.c).
//! Inputs: a random actor (the bitfields at +8 and +0x5AC, unk5B9, unkF, the
//! position) and a 4-byte target. target[1] picks the anchor path (func_800A4A24,
//! stubbed to an arena SVECTOR) or the ground path (func_800A3500, stubbed to a
//! chosen height). The launch differences (0, -64, 128, speed * frames so that
//! ABS(vx) == speed, small and occasionally large values) are chosen directly,
//! so the ordered comparisons see equal sides. The hang-frame tables
//! D_800E90AC are set nonzero (frames is a divisor), and D_800E90B4 is zeroed
//! where the path needs exact heights. The actor is always valid: the C never
//! tests it for null. func_800AE4FC and func_800A0204 change the flags word
//! the C reads again after them (see the stubs).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").startJump));

pub const classes = [_][:0]const u8{ "early-return", "anchor-path", "airborne", "grounded", "clamped" };

/// The SVECTOR the anchor stub hands out, and the height func_800A3500 returns.
var anchor: ?*v.SVECTOR = null;
var ground_y: c_int = 0;
/// The actor of this iteration (func_800A0204 takes its slot, not a pointer).
var the_actor: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_800A4A24(ctx: *zd.Ctx, index: c_int) [*c]c.SVECTOR {
        _ = ctx;
        _ = index;
        return @ptrCast(anchor.?);
    }

    /// The C reads the flags word at +8 again after this call: half the time
    /// it flips the 0x400000 (fast) bit, so a port that keeps the value it
    /// read before the call differs.
    pub fn func_800AE4FC(ctx: *zd.Ctx, unk0: anytype, kind: c_int) void {
        _ = kind;
        if (ctx.value(bool)) psx.byteAt(u32, unk0.?, 8).* ^= 0x400000;
    }

    /// The final flags store reads the word at +8 after this call: it changes
    /// bits there (and unkF, which the next call passes), as an animation
    /// start may.
    pub fn func_800A0204(ctx: *zd.Ctx, slot: c_int, animation: c_int, a: c_int, b: c_int) void {
        _ = slot;
        _ = animation;
        _ = a;
        _ = b;
        const actor = the_actor.?;
        psx.byteAt(u32, actor, 8).* ^= ctx.value(u32);
        actor.unk0.unkF +%= 1;
    }

    pub fn func_800A3500(ctx: *zd.Ctx, x: c_int, z: c_int) c_int {
        _ = ctx;
        _ = x;
        _ = z;
        return ground_y;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    the_actor = actor;
    const target: [*c]u8 = ctx.allocBytes(4, 4);
    target[0] = ctx.int(u8);
    target[1] = if (ctx.chance(2)) ctx.pick(u8, &.{2}) else ctx.range(u8, 0, 1);
    target[2] = ctx.int(u8);
    target[3] = ctx.int(u8);

    // Early return (the 0x70000 bits of the word at +8) in about 1/6 of runs; the
    // 0x400000 flag (unkA_6) picks the fast-actor branches.
    const fast: bool = ctx.int(u1) != 0;
    psx.setBits(&actor.unk0, "unkA_6", @intFromBool(fast));
    psx.setBits(&actor.unk0, "unkA_0", if (ctx.chance(6)) ctx.range(u3, 1, 7) else 0);
    actor.unk0.unkF = if (ctx.chance(3)) 0 else ctx.int(u8);
    actor.unk5B9 = ctx.int(u8);

    var speed: c_int = if (ctx.chance(2))
        ctx.range(c_int, -10, 3000)
    else
        ctx.pick(c_int, &.{ 17, 18, 19, 0, -1, 0x7fff_ffff, -0x8000_0000 });
    if (ctx.chance(4)) speed = actor.unk5B9;

    // The ABS(vx) > speed equality: frames is exact, so x = speed * frames gives vx == speed.
    const equal_abs = ctx.chance(6);
    if (equal_abs) speed = ctx.range(c_int, 18, 1500);

    const dY: c_int = if (equal_abs) 0 else switch (ctx.random.uintLessThan(u8, 8)) {
        0, 1 => 0,
        2 => -64,
        3 => 128,
        else => ctx.range(c_int, -300, 300),
    };

    // Hang-frame tables: frames (a divisor) is never 0; D_800E90B4 zeroed where the
    // exact equalities need it.
    const ac = v.g.D_800E90AC();
    const bc = v.g.D_800E90B4();
    const zero_b = equal_abs or dY == 128;
    for (0..6) |k| {
        ac[k] = ctx.range(u8, 1, 12);
        bc[k] = if (zero_b) 0 else ctx.range(u8, 0, 40);
    }

    const idx: usize = if (fast) 4 else 1;
    const frames: c_int = ac[idx];
    var dX: c_int = if (equal_abs)
        speed * frames
    else if (ctx.chance(3))
        0
    else
        ctx.range(c_int, -2000, 2000);
    if (equal_abs and ctx.chance(2)) dX = -dX;
    // Large differences make the retry loop run (rarely, to keep the loop cheap).
    if (!equal_abs and ctx.chance(40)) dX = ctx.range(c_int, -8000, 8000);

    const dZ: c_int = if (equal_abs) 0 else switch (ctx.random.uintLessThan(u8, 6)) {
        0 => dX,
        1 => -dX,
        2 => 0,
        else => ctx.range(c_int, -2000, 2000),
    };

    const gx: c_int = @as(c_int, target[0]) * 128 + 64;
    const gz: c_int = @as(c_int, target[2]) * 128 + 64;

    var px: i16 = if (ctx.chance(4)) ctx.int(i16) else ctx.range(i16, -1000, 1000);
    const py: i16 = if (ctx.chance(4)) ctx.int(i16) else ctx.range(i16, -1000, 1000);
    var pz: i16 = if (ctx.chance(4)) ctx.int(i16) else ctx.range(i16, -1000, 1000);

    // The anchor (used when target[1] >= 2): anchor minus position gives (dX, dY, dZ).
    const a = ctx.alloc(v.SVECTOR);
    a.vx = @truncate(@as(c_int, px) + dX);
    a.vy = @truncate(@as(c_int, py) + dY);
    a.vz = @truncate(@as(c_int, pz) + dZ);
    a.pad = ctx.int(i16);
    anchor = a;

    // The ground height: height minus position gives dY.
    ground_y = @as(c_int, py) + dY;

    // The ground path: (gx, gz) minus position gives (dX, dZ).
    if (target[1] < 2) {
        px = @truncate(gx - dX);
        pz = @truncate(gz - dZ);
    }

    actor.unk0.position.vx = px;
    actor.unk0.position.vy = py;
    actor.unk0.position.vz = pz;

    const facing: i16 = ctx.int(i16);
    return .{ actor, target, speed, facing };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if (psx.getBits(&actor.unk0, "unkA_0") != 0) return 0;
    if (args[1][1] >= 2) return 1;
    const y = ground_y - @as(c_int, actor.unk0.position.vy);
    if (y <= 0) return 3;
    if (psx.getBits(&actor.unk0, "unkA_6") != 0 and y > 128) return 4;
    return 2;
}
