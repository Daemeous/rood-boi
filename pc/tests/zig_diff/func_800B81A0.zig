//! zig_diff spec: func_800B81A0 -> func_800B81A0.func_800B81A0 (4A0A8.c).
//! Inputs: a 8-byte block in the arena whose bytes 3, 4 and 5 (the three
//! sp10 fields) are zero in one class, nonzero in exactly one of them in
//! three classes (with the 0x80 and 0xFF edges, so the bit-7 cases of each
//! byte are hit), or nonzero in all three; bytes 1 and 2 (the short
//! argument) and arg1 are random. The OR of three bytes is never negative
//! as an int, so only the nonzero side of the sign-bit flag exists here.
//! The callee func_800A07FC is a stub that copies the bytes its pointer
//! argument points at into an arena log (the C reads nothing back, but
//! the callee reads the struct), so a wrong sp10 field shows as a state
//! mismatch; a null pointer is logged as a record with flag 0. Its return
//! value is drawn from ctx.value (the C ignores it). The callee
//! func_800BFE50 and vs_battle_getShort use the default stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B81A0));

pub const classes = [_][:0]const u8{ "all-zero", "unk0-only", "unk1-only", "unk2-only", "several" };

/// Arena block the callee's stub writes its record into (compared as state).
var log_block: [*]u8 = undefined;

fn addrOf(p: anytype) usize {
    return switch (@typeInfo(@TypeOf(p))) {
        .null => 0,
        .optional => if (p) |q| @intFromPtr(q) else 0,
        .pointer => @intFromPtr(p),
        else => @intCast(p),
    };
}

pub const stubs = struct {
    pub fn func_800A07FC(ctx: *zd.Ctx, a: anytype, p: anytype) c_int {
        _ = a;
        const addr = addrOf(p);
        if (addr == 0) {
            log_block[0] = 0;
            log_block[1] = 0;
            log_block[2] = 0;
            log_block[3] = 0;
        } else {
            const s: *align(1) const v.D_800F4538_unk58_2 = @ptrFromInt(addr);
            log_block[0] = s.unk0;
            log_block[1] = s.unk1;
            log_block[2] = s.unk2;
            log_block[3] = 1;
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(8, 1);
    log_block = ctx.allocBytes(8, 8);
    buf[1] = ctx.int(u8);
    buf[2] = ctx.int(u8);
    buf[3] = 0;
    buf[4] = 0;
    buf[5] = 0;
    switch (ctx.range(u8, 0, 4)) {
        0 => {},
        1 => buf[3] = ctx.edge(u8, &.{ 1, 0x80, 0xFF }),
        2 => buf[4] = ctx.edge(u8, &.{ 1, 0x80, 0xFF }),
        3 => buf[5] = ctx.edge(u8, &.{ 1, 0x80, 0xFF }),
        else => {
            buf[3] = ctx.edge(u8, &.{ 1, 0x80, 0xFF });
            buf[4] = ctx.edge(u8, &.{ 1, 0x80, 0xFF });
            buf[5] = ctx.edge(u8, &.{ 1, 0x80, 0xFF });
        },
    }
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const a = args[0];
    const nz0 = a[3] != 0;
    const nz1 = a[4] != 0;
    const nz2 = a[5] != 0;
    if (!nz0 and !nz1 and !nz2) return 0;
    if (@as(u8, @intFromBool(nz0)) + @intFromBool(nz1) + @intFromBool(nz2) > 1) return 4;
    if (nz0) return 1;
    if (nz1) return 2;
    return 3;
}
