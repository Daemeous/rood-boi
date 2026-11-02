//! zig_diff spec: func_800CF218 -> func_800CF218.func_800CF218 (5BF94.c).
//! Inputs: an effect request in a 4 KiB arena block (its targets are 12
//! bytes apart from offset 16, past the request itself: the C does not
//! bound them), its count 0..3 or 255, its id at the class edges (23, 25,
//! 40, 41, 44, 53, 54 and their neighbours). func_800CF55C gets the C's
//! stack spawn record (a tag): its stub logs the fields the C sets (the
//! rest is uninitialised) and returns one arena child script.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CF218));

pub const classes = [_][:0]const u8{ "none", "second-first", "first-second" };

var log: ?[*][9]u32 = null;
var child: ?*v.D_800F53B8_t = null;

pub const stubs = struct {
    pub fn func_800CF55C(ctx: *zd.Ctx, spawn: anytype) ?*v.D_800F53B8_t {
        const s: *v.D_800F53B8_t2 = @ptrCast(@alignCast(spawn));
        const k = @min(@as(usize, ctx.calls) - 1, 255); // after func_800CF49C
        log.?[k] = .{ s.unk0.raw, s.unk4.raw, s.unk8.raw, s.unkC.raw, @bitCast(s.unk10), s.unk14.raw, @bitCast(s.unk18), s.unk1C.raw, s.unk20 };
        return child;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(256 * 36, 16)));
    child = ctx.alloc(v.D_800F53B8_t);
    const pf = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = pf;
    const request: *v.func_800CF0E8_t = @ptrCast(@alignCast(ctx.allocBytes(4096, 16)));
    request.unk2 = ctx.pick(u8, &.{ 0, 1, 2, 3, 3, 255 });
    if (request.unk2 > 3 and request.unk2 != 255) request.unk2 = 3;
    request.effectId = ctx.edge(u16, &.{ 22, 23, 24, 25, 26, 39, 40, 41, 42, 43, 44, 53, 54, 0 });
    return .{ request, ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const r = args[0].?;
    if (r.unk2 == 0) return 0;
    const id = r.effectId;
    if ((id >= 40 and id <= 41) or (id >= 44 and id <= 53) or id == 23 or id == 25) return 1;
    return 2;
}
