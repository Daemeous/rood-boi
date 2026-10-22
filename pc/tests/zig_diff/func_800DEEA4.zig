//! zig_diff spec: func_800DEEA4 -> func_800DEEA4.func_800DEEA4 (74088.c).
//! Inputs: a null argument (1 in 8), or an arena func_800DEEA4_t whose unk44 is
//! 0 about a third of the time and otherwise an edge value (1, -1, INT_MIN,
//! INT_MAX) or a random i32, and whose unk4 indexes D_800F5878 in 0..17 (the
//! table's 18 entries). The table slot for unk4 is null (1 in 3 of the
//! non-null cases) or an arena func_800DEEA4_t2 with random bytes, so the
//! zero-unk44 path sees both a null and a live entry. func_800DC810 is a
//! recording stub that sometimes rewrites unk44 and the entry's unk9A through
//! ctx.value, so a port that reads either after the call, or moves the store
//! across it, diverges. Out-of-range unk4 (18..255) is not built: it would
//! read past the table's 144 bytes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DEEA4));

pub const classes = [_][:0]const u8{ "null-arg", "nonzero-unk44", "zero-unk44-null-entry", "zero-unk44-entry" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(8)) return .{null};
    const obj = ctx.alloc(v.func_800DEEA4_t);
    if (ctx.chance(2)) {
        obj.unk44 = 0;
    } else {
        obj.unk44 = ctx.pick(i32, &.{ 1, -1, std.math.minInt(i32), std.math.maxInt(i32) });
    }
    obj.unk4 = ctx.range(u8, 0, 17);
    const tbl = v.g.D_800F5878();
    if (ctx.chance(3)) {
        tbl[obj.unk4] = null;
    } else {
        const e = ctx.alloc(v.func_800DEEA4_t2);
        tbl[obj.unk4] = e;
    }
    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0] orelse return 0;
    if (obj.unk44 != 0) return 1;
    const tbl = v.g.D_800F5878();
    return if (tbl[obj.unk4] == null) 2 else 3;
}

pub const stubs = struct {
    pub fn func_800DC810(ctx: *zd.Ctx, p: anytype) void {
        const a: *v.func_800DEEA4_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) a.unk44 = ctx.value(i32);
        if (ctx.value(u1) == 1) {
            const tbl = v.g.D_800F5878();
            if (a.unk4 < 18) {
                if (tbl[a.unk4]) |e| {
                    e.unk9A = ctx.value(i16);
                }
            }
        }
    }
};
