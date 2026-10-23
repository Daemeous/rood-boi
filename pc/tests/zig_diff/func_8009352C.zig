//! zig_diff spec: func_8009352C -> func_8009352C.func_8009352C (2842C.c).
//! Inputs: arg1 is a random-filled func_80092F74_t2 whose first slot's u16[0]
//! and u16[1] are drawn from edges (zero, sign bit set, sign bit clear, all
//! ones) and random values; arg0 is unused, so it is null or a random object;
//! roomData.section14 is null or points at an arena word the callees may
//! write; D_800F1D00 is 0, 1, -1 or -32768 (a zero test that sees the sign
//! bit both ways). The two callees are stubs that may write the pointee and
//! D_800F2258, so a port that stores before the calls, or reads the global
//! back, is caught.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009352C));

pub const classes = [_][:0]const u8{ "null-section", "first-half-zero", "flag-set", "flag-clear" };

pub const stubs = struct {
    pub fn func_800462E8(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) void {
        _ = a;
        _ = c;
        if (ctx.value(u1) == 1 and b != null) {
            const p: *u32 = @ptrCast(@alignCast(b));
            p.* = ctx.value(u32);
        }
        if (ctx.value(u1) == 1) v.g.D_800F2258().* = ctx.value(i16);
    }

    pub fn func_800461CC(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype, e: anytype) void {
        _ = a;
        _ = c;
        _ = d;
        _ = e;
        if (ctx.value(u1) == 1 and b != null) {
            const p: *u32 = @ptrCast(@alignCast(b));
            p.* = ctx.value(u32);
        }
        if (ctx.value(u1) == 1) v.g.D_800F2258().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    const block = ctx.alloc(u32);
    psx.put(&room.section14, if (ctx.chance(6)) null else block);

    const arg0: ?*v.func_80092F74_t = if (ctx.chance(2)) null else ctx.alloc(v.func_80092F74_t);
    const arg1 = ctx.alloc(v.func_80092F74_t2);
    arg1.unk4[0].u16[0] = ctx.pick(u16, &.{ 0, 0, 1, 0x8000, 0xFFFF });
    arg1.unk4[0].u16[1] = ctx.pick(u16, &.{ 0, 0x80, 0x200, 0x8000, 0xFFFF });

    v.g.D_800F1D00().* = ctx.pick(i16, &.{ 0, 1, -1, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const room = v.g.vs_battle_roomData();
    if (psx.get(room.section14) == null) return 0;
    if (args[1].?.unk4[0].u16[0] == 0) return 1;
    if (v.g.D_800F1D00().* != 0) return 2;
    return 3;
}
