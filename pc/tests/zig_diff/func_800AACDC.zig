//! zig_diff spec: func_800AACDC -> func_800AACDC.func_800AACDC (40564.c).
//! The C has no branches. Its inputs are the scratchpad slot 0 (the DR_MOVE
//! pointer it reads): null, a DR_MOVE in the arena (most runs), or any other
//! 32-bit value up to 0xFFFF0000. The stubs write the DR_MOVE only when it
//! is an aligned arena object, so a random pointer is never dereferenced.
//! SetDrawMove copies the RECT it is handed into the DR_MOVE's code words,
//! so the RECT fields the C stores (x, y, w, h) reach the compared state.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
const psx = @import("common").psx;
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AACDC));

pub const classes = [_][:0]const u8{ "null-slot", "arena-slot", "other-slot" };

fn slotPtr() *align(4) u32 {
    return @ptrFromInt(zd.scratchpad);
}

fn arenaBase() u32 {
    return @intCast(@intFromPtr(&zd.zdiff_arena));
}

/// The DR_MOVE at p when it is an aligned object wholly inside the arena.
fn inArena(p: anytype) ?*v.DR_MOVE {
    const a: usize = @intFromPtr(p);
    const base: usize = @intFromPtr(&zd.zdiff_arena);
    if (a < base or a + @sizeOf(v.DR_MOVE) > base + zd.arena_size) return null;
    if (a % @alignOf(v.DR_MOVE) != 0) return null;
    return @ptrFromInt(a);
}

pub const stubs = struct {
    pub fn SetDrawMove(ctx: *zd.Ctx, p: anytype, rect: anytype, x: anytype, y: anytype) void {
        _ = x;
        _ = y;
        const dm = inArena(p) orelse return;
        dm.tag = ctx.value(u32);
        for (&dm.code) |*w| w.* = ctx.value(u32);
        // The rect the C built on its stack: its four fields become visible
        // in the DR_MOVE, so a wrong width or height changes the state.
        const r: *align(1) v.RECT = @ptrCast(rect);
        const xy: u32 = @as(u32, @as(u16, @bitCast(r.x))) | (@as(u32, @as(u16, @bitCast(r.y))) << 16);
        const wh: u32 = @as(u32, @as(u16, @bitCast(r.w))) | (@as(u32, @as(u16, @bitCast(r.h))) << 16);
        dm.code[0] = xy;
        dm.code[1] = wh;
    }

    pub fn func_8007A824(ctx: *zd.Ctx, p: anytype) void {
        const dm = inArena(p) orelse return;
        dm.tag = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const dm = ctx.alloc(v.DR_MOVE);
    const slot = slotPtr();
    if (ctx.chance(8)) {
        slot.* = 0;
    } else if (ctx.chance(6)) {
        // Any word-aligned pointer value below 4 GB minus 48, so temp + 1 stays a
        // guest address (packets are word-aligned, as a DR_MOVE* must be).
        slot.* = ctx.range(u32, 0, 0x3FFF_C000) * 4;
    } else {
        slot.* = @intCast(psx.addr(dm));
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const val = slotPtr().*;
    if (val == 0) return 0;
    const base = arenaBase();
    if (val >= base and val < base + zd.arena_size) return 1;
    return 2;
}
