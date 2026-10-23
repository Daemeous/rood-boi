//! zig_diff spec: func_800A41D0 -> func_800A41D0.func_800A41D0 (3A1A0.c).
//! Inputs: an actor and an object in the arena. The object is reached through
//! the D_800F45E0 table at a random index 0-15 (other entries stay random and
//! are never read). Modes 2, 1, and others (0, 3, -1) are all built, the object
//! is null on the non-mode path a quarter of the time, and its byte at +8
//! (bits 8-11, the 0xF00 test) is zero or not. The 0x16C byte covers every
//! switch case, default, and both sides of the 0x30 test. D_800F49E8 is near
//! 0..4 often (the < 2 and & 1 tests, the == 2 edge). Unit tile coordinates sit
//! on 0/1/254/255 (the i16 wrap of unk1C/unk20). Callees write back what the C
//! reads: the 0x16C byte, unk26/unk34/unk38 after func_800E68A0, the points
//! out-parameter (vy, read back by the average), and D_800F49E8 after
//! func_800E4C28.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A41D0));

pub const classes = [_][:0]const u8{ "mode2", "mode1", "no-object", "flagged", "main" };

pub const stubs = struct {
    pub fn func_800E68A0(ctx: *zd.Ctx, obj: anytype) void {
        const o: *v.D_800F45E0_t = @ptrCast(@alignCast(obj));
        if (ctx.value(u1) == 1) psx.byteAt(u8, o, 0x16C).* = ctx.value(u8);
        if (ctx.value(u1) == 1) o.unk26 = ctx.value(i16);
        if (ctx.value(u1) == 1) o.unk34 = ctx.value(i16);
        if (ctx.value(u1) == 1) o.unk38 = ctx.value(i16);
    }

    pub fn func_800A190C(ctx: *zd.Ctx, id: anytype, slot: anytype, out: anytype, n: anytype) c_int {
        _ = id;
        _ = slot;
        _ = n;
        const p: *v.SVECTOR = @ptrCast(@alignCast(out));
        p.vx = ctx.value(i16);
        p.vy = ctx.value(i16);
        p.vz = ctx.value(i16);
        return 0;
    }

    // The prototype is variadic, so the stub takes no arguments of its own:
    // it only writes back the global the C reads after the call.
    pub fn func_800E4C28(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F49E8().* = @as(i32, ctx.value(i8));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const object = ctx.alloc(v.D_800F45E0_t);

    const m = ctx.range(u8, 0, 9);
    const mode: c_int = switch (m) {
        0, 1 => 1,
        2, 3 => 2,
        4 => 3,
        5 => -1,
        else => 0,
    };

    const idx = ctx.range(usize, 0, 15);
    v.g.D_800E90A8().* = @intCast(idx);
    const no_object = mode != 1 and mode != 2 and ctx.chance(4);
    v.g.D_800F45E0().*[idx] = if (no_object) null else object;

    v.g.D_800F49E8().* = if (ctx.chance(2))
        ctx.range(i32, -8, 8)
    else
        ctx.pick(i32, &.{ 0, 1, 2, 3, 4, -1, -2, -3, 100, -100 });

    actor.unk0.currentTileX = ctx.pick(u8, &.{ 0, 1, 254, 255 });
    actor.unk0.currentTileZ = ctx.pick(u8, &.{ 0, 1, 254, 255 });

    if (!no_object) {
        object._bits_8[1] = if (ctx.chance(2)) ctx.range(u8, 1, 15) else ctx.int(u8) & 0xF0;
        psx.byteAt(u8, object, 0x16C).* = ctx.pick(u8, &.{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x07, 0x10, 0x20, 0x30, 0xFF });
        object.unk34 = ctx.pick(i16, &.{ 0x7FFF, -0x8000, 0, -1, 0x7F80 });
        object.unk38 = ctx.pick(i16, &.{ 0x7FFF, -0x8000, 0, -1, 0x7F80 });
    }

    // The actor is not read on the mode 2 path.
    const actor_arg: ?*v.D_800F4538_t = if (mode == 2 and ctx.chance(8)) null else actor;
    return .{ actor_arg, mode };
}

pub fn classify(args: Args) usize {
    const mode = args[1];
    if (mode == 2) return 0;
    if (mode == 1) return 1;
    const idx: usize = @intCast(v.g.D_800E90A8().*);
    const object = v.g.D_800F45E0().*[idx] orelse return 2;
    if ((object._bits_8[1] & 0x0F) != 0) return 3;
    return 4;
}
