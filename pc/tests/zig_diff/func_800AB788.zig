//! zig_diff spec: func_800AB788 -> func_800AB788.func_800AB788 (40564.c).
//! Inputs: the object path (actor null: 16 colours at object+0x12C, state at
//! object+0x64, slot object[15]), an actor whose 0x8000000 flag is set (160
//! colours) or clear (16 colours), or an object passed with a non-null actor
//! (the C does not read it then; it may also be null). The state byte is
//! often 12-16 with its bit 2 clear, so the fade steps land exactly on 16
//! (the `value >= 16` compare sees equal sides) and the full-brightness
//! branch runs; other values give decrements past 0 and negatives. The tick
//! speed is 4 or not. Colours are edges (0, 0x8000, 0x7FFF, 0xFFFF, 1, 0x0421)
//! or random.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AB788));

pub const classes = [_][:0]const u8{ "object", "actor-count16", "actor-count160" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.vs_gametime_tickspeed().* = ctx.pick(i32, &.{ 4, 4, 0, 3, 5, -1 });

    const obj: [*c]u8 = ctx.allocBytes(0x400, 16);
    var actor: [*c]u8 = null;
    if (!ctx.chance(3)) {
        const a: [*c]u8 = ctx.allocBytes(0x1800, 16);
        actor = a;
        const flags = psx.byteAt(u32, a, 0x5AC);
        const bit: u32 = 0x0800_0000;
        flags.* = if (ctx.chance(2)) (ctx.int(u32) | bit) else (ctx.int(u32) & ~bit);
    }

    const owner: [*c]u8 = if (actor != null) actor else obj;

    // Near 16 half the time: a fade step from 14 (or 12, 15) reaches exactly 16.
    psx.byteAt(u8, owner, 0x65).* = if (ctx.chance(2))
        ctx.edge(u8, &.{ 11, 12, 13, 13, 14, 15, 16 })
    else
        ctx.pick(u8, &.{ 0, 1, 2, 17, 30, 31, 254, 255 });
    // Bit 2 clear three times in four, so the increment path runs.
    const st2 = psx.byteAt(u8, owner, 0x66);
    st2.* = if (ctx.chance(4)) (ctx.int(u8) | 2) else (ctx.int(u8) & ~@as(u8, 2));

    const ncol: usize = if (actor == null) 16 else 160;
    const colOff: isize = if (actor == null) 0x12C else 0x1424;
    for (0..ncol) |i| {
        const off: isize = colOff + @as(isize, @intCast(2 * i));
        psx.byteAt(u16, owner, off).* = ctx.pick(u16, &.{ 0, 0x8000, 0x7FFF, 0xFFFF, 1, 0x0421 });
    }

    const obj_arg: [*c]u8 = if (actor != null and ctx.chance(4)) null else obj;
    return .{ actor, obj_arg, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    if (args[0] == null) return 0;
    const flags = psx.byteAt(u32, args[0], 0x5AC).*;
    return if ((flags & 0x0800_0000) != 0) 2 else 1;
}
