//! zig_diff spec: func_800A2C48 -> func_800A2C48.func_800A2C48 (3A1A0.c).
//! Inputs: a root D_800F4538_t in the arena whose unk17FD is 2 (a third of
//! the runs) or 0-16. The global table D_800F4538 holds two arena children at
//! indices 2..16 (only 2..16 are ever indexed: the C indexes past the 17-entry
//! table otherwise). A child's unk17FD is 0 or 1 at prepare, and the
//! func_800A29A0 stub rewrites it to 0 or 1, so the recursion stops one level
//! down and never cycles. The root's unk17FD is rewritten to 0-16 by the same
//! stub, so the C's post-call read takes every table index. func_800A9C54's
//! stub writes 8 bytes at its pointer and returns a value the C ignores.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A2C48));

pub const classes = [_][:0]const u8{ "no-recurse", "recurse-2", "recurse-3-16" };

var root: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_800A9C54(ctx: *zd.Ctx, unkF: anytype, p: anytype, n: anytype) u32 {
        _ = unkF;
        _ = n;
        const b: *align(1) [2]u32 = @ptrCast(p.?);
        b.* = .{ ctx.value(u32), ctx.value(u32) };
        return ctx.value(u32);
    }

    pub fn func_800A29A0(ctx: *zd.Ctx, p: anytype) void {
        const obj: *v.D_800F4538_t = @ptrCast(@alignCast(p.?));
        const is_root = @intFromPtr(obj) == @intFromPtr(root.?);
        // The root may take any table index 0-16 the C can recurse into;
        // a child always becomes terminal (0 or 1), so no cycle forms.
        obj.unk17FD = if (is_root) ctx.value(u8) % 17 else ctx.value(u8) & 1;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const r = ctx.alloc(v.D_800F4538_t);
    const k1 = ctx.alloc(v.D_800F4538_t);
    const k2 = ctx.alloc(v.D_800F4538_t);
    root = r;
    k1.unk17FD = ctx.range(u8, 0, 1);
    k2.unk17FD = ctx.range(u8, 0, 1);
    r.unk17FD = if (ctx.chance(3)) 2 else ctx.range(u8, 0, 16);
    const table = v.g.D_800F4538();
    var i: usize = 2;
    while (i < 17) : (i += 1) {
        table[i] = if (ctx.chance(2)) k1 else k2;
    }
    return .{r};
}

pub fn classify(args: Args) usize {
    const u = args[0].?.unk17FD;
    if (u < 2) return 0;
    if (u == 2) return 1;
    return 2;
}
