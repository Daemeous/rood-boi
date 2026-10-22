//! zig_diff spec: func_800AFDE8 -> func_800AFDE8.func_800AFDE8 (44F14.c).
//! Inputs: the scratchpad animationFrameScratch with a frame window that is
//! sometimes empty (last below first), a delta record at value+0x2A0, and a
//! random bytecode buffer in the arena. The C reaches the buffer through
//! value->pad (a u_short) plus an offset word: the spec picks a random pad
//! P and stores word = (base - P) - offset, so the C's data pointer starts at
//! the buffer's base whatever the offset. The next frame sits at the window
//! start or a little past it (equal to the frame often), flags are random so
//! every record form, the 0x1F run, the packed-delta paths and flags == 0
//! occur, and rotation is zero, nonzero or a sign-bit value.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
const c = @import("c_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AFDE8));

pub const classes = [_][:0]const u8{ "no-frames", "rotation", "plain" };

const buf_len = 8192;
const scratch_addr: usize = zd.scratchpad + 0x3EC;

fn scratch() *c.animationFrameScratch {
    return @ptrFromInt(scratch_addr);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = scratch();
    const value: *v.SVECTOR = @ptrCast(@alignCast(ctx.allocBytes(0x2A8, 16)));
    const delta: *v.SVECTOR = @ptrFromInt(@intFromPtr(value) + 0x2A0);

    // The bytecode buffer; its random fill is the record stream.
    const bytes = ctx.allocBytes(buf_len, 16);
    const base: u32 = psx.addr(bytes);
    const pad: u16 = ctx.int(u16);
    const offset = ctx.pick(c_int, &.{ 0, 1, -1, 0x1000, 0x7fff_ffff, -0x8000_0000 });
    const word = ctx.alloc(u32);
    word.* = (base -% @as(u32, pad)) -% @as(u32, @bitCast(offset));
    value.pad = @bitCast(pad);
    psx.put(&s.data, word);

    const first: i32 = ctx.range(i32, -2, 40);
    const last: i32 = if (ctx.chance(6))
        first - ctx.range(i32, 1, 3)
    else
        first + ctx.range(i32, 0, 24);
    s.first = @intCast(first);
    s.last = @intCast(last);

    const next: i32 = if (ctx.chance(16)) 65535 else first + ctx.range(i32, -3, 30);
    delta.pad = @bitCast(@as(u16, @intCast(@max(0, next))));

    const rotation = ctx.pick(c_int, &.{ 0, 0, 1, -1, 0x7fff_ffff, -0x8000_0000 });
    return .{ offset, value, rotation };
}

pub fn classify(args: Args) usize {
    const s = scratch();
    if (s.first > s.last) return 0;
    return if (args[2] != 0) 1 else 2;
}
