//! zig_diff spec: func_800A11D8 -> func_800A11D8.func_800A11D8 (30DB0.c).
//! Inputs: an index 0-15 (or 16 when the first table holds the object), the
//! first table entry an arena object, a fallback object, or neither (the
//! early return). The object's unk68 block is an arena block with random
//! armature bytes, and the bone index runs 0-41. Both null tests are reached
//! on every path; the port must read the same table entries and armature
//! word, and store the same bone and word.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A11D8));

pub const classes = [_][:0]const u8{ "object", "fallback", "both-null" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    // unk68 points at a block with room for 64 header bytes and armatures[0..63].
    const blk: *v.D_800F4538_unk68 = @ptrCast(@alignCast(ctx.allocBytes(64 + 16 * 64, 16)));
    psx.put(&obj.unk0.unk68, blk);

    // 0: first table holds obj; 1: only the fallback table holds obj; 2: neither.
    const mode = ctx.range(u8, 0, 2);
    var arg0: c_int = ctx.range(c_int, 0, 15);
    if (mode == 0 and ctx.chance(4)) arg0 = 16;
    const idx: usize = @intCast(arg0);

    v.g.D_800F4538()[idx] = if (mode == 0) obj else null;
    if (idx < 16) {
        const other: *v.D_800F45E0_t = @ptrCast(obj);
        v.g.D_800F45E0()[idx] = if (mode == 1) other else null;
    }

    const mat = ctx.alloc(v.MATRIX);
    const out = ctx.alloc(c_uint);
    const arg1: c_int = ctx.range(c_int, 0, 41);
    return .{ arg0, arg1, mat, out };
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    if (v.g.D_800F4538()[idx] != null) return 0;
    if (idx < 16 and v.g.D_800F45E0()[idx] != null) return 1;
    return 2;
}
