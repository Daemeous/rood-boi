//! zig_diff spec: func_800D7C5C -> func_800D7C5C.func_800D7C5C (6F014.c).
//! Inputs: the pending-flag word D_800F5874 (random bytes, plus edges such as
//! 0, -1, INT_MIN and values with only some of bits 24-31 set) and the heap
//! pointer vs_battle_pfileBuf (null or a block in the arena). The callee
//! func_8007E0A8 may rewrite D_800F5874 (the C re-reads it each pass) and
//! returns a random int, which the C ignores. vs_main_freeHeapR is a
//! recording stub that changes nothing.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7C5C));

pub const classes = [_][:0]const u8{ "pfile-null", "pfile-set-high-flags-clear", "pfile-set-high-flags-set" };

pub const stubs = struct {
    pub fn func_8007E0A8(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        // The callee can clear or set pending flags: the C re-reads the word.
        if (ctx.value(u1) == 1) v.g.D_800F5874().* = ctx.value(i32);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const pfile = v.g.vs_battle_pfileBuf();
    if (ctx.chance(3)) {
        pfile.* = null;
    } else {
        pfile.* = @ptrCast(ctx.alloc(u8));
    }
    const flags = v.g.D_800F5874();
    flags.* = ctx.pick(i32, &.{
        0,
        -1,
        -2147483648,
        0x01000000,
        0x7FFFFFFF,
        0x0F000000,
        0x70000000,
        @as(i32, @bitCast(@as(u32, 0x80000000))),
    });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const pfile = v.g.vs_battle_pfileBuf().*;
    if (pfile == null) return 0;
    const flags = v.g.D_800F5874().*;
    return if (flags & @as(i32, @bitCast(@as(u32, 0xFF000000))) == 0) 1 else 2;
}
