//! zig_diff spec: func_800D4E5C -> func_800D4E5C.func_800D4E5C (5BF94.c).
//! Inputs: arg0 is either null or a random D_800F53B8_t object in the arena.
//! The C never dereferences arg0; it only passes it on, so null is valid.
//! The two callees are recording stubs. func_800D5198 returns any u16 and
//! may rewrite a field of its object; func_800D6AEC may rewrite another
//! field. Both stubs skip the object when it is null.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4E5C));

pub const classes = [_][:0]const u8{ "null-arg", "object-arg" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(4)) return .{null};
    return .{ctx.alloc(v.D_800F53B8_t)};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}

pub const stubs = struct {
    // Returns the value the C passes on; may rewrite the object's unkA.
    pub fn func_800D5198(ctx: *zd.Ctx, p: anytype) c_ushort {
        if (p) |raw| {
            const a: *v.D_800F53B8_t = @ptrCast(@alignCast(raw));
            if (ctx.value(u1) == 1) a.unkA = ctx.value(u16);
        }
        return ctx.value(c_ushort);
    }

    // May rewrite the object's unk8 (re-read by the C after the call).
    pub fn func_800D6AEC(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = n;
        if (p) |raw| {
            const a: *v.D_800F53B8_t = @ptrCast(@alignCast(raw));
            if (ctx.value(u1) == 1) a.unk8 = ctx.value(u8);
        }
    }
};
