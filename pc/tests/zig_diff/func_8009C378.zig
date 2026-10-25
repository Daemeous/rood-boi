//! zig_diff spec: func_8009C378 -> func_8009C378.func_8009C378 (30DB0.c).
//! Inputs: two func_8009C378_t objects in the arena, each with random bytes
//! (so the 0x7800-byte copy and the two relocations are checked against real
//! data), in either address order, or the same object (the copy is a no-op
//! and both relocations come out zero). The unk0 and unk4 guest pointers are
//! random 32-bit values, so the relocation's addition wraps. vs_main_memcpy
//! is a stub that really copies, so a port that reads the fields before the
//! copy, or copies the wrong length, is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009C378));

pub const classes = [_][:0]const u8{ "distinct", "same-object" };

/// The bytes of a pointer, optional or not.
fn bytesOf(p: anytype) [*]u8 {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const n: usize = @intCast(sz);
        const d = bytesOf(dest);
        const s = bytesOf(src);
        // Same block: the real copy is a no-op, and @memcpy rejects aliasing.
        if (@intFromPtr(d) == @intFromPtr(s)) return;
        @memcpy(d[0..n], s[0..n]);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a = ctx.alloc(v.func_8009C378_t);
    if (ctx.chance(5)) return .{ a, a };
    const b = ctx.alloc(v.func_8009C378_t);
    // Both address orders: a below b in the arena, or above it.
    if (ctx.chance(2)) return .{ b, a };
    return .{ a, b };
}

pub fn classify(args: Args) usize {
    return if (args[0] == args[1]) 1 else 0;
}
