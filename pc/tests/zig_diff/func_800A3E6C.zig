//! zig_diff spec: func_800A3E6C -> func_800A3E6C.func_800A3E6C (3A1A0.c).
//! Inputs: one actor, shared by every D_800F4538 slot, whose 0x5AC word has
//! the busy bit (0x100000) on a fifth of the time and bit 3 either way; one
//! object, shared by every D_800F45E0 slot, whose mode byte (0x16C & 7) takes
//! every value 0..7, so each switch arm and the arm-less 7 run. The 0/1 arms
//! index D_800E8FD0 with the object's 0x16E byte kept at 0..23 and
//! D_800F49E8 at 0..3, which keeps the index inside the 96 entries. Elsewhere
//! D_800F49E8 takes edges (2 sits at the `< 2` boundary, -1 and INT_MIN carry
//! the sign bit). The actor's tile sits at the u8 wrap points and its height
//! is random, so the signed `/ 64` sees negatives. The stubs return -1 or an
//! object index 0..15 from func_800A38E0, and write the actor's 0x5AC word,
//! position and tile, which the C reads back after the calls. Each callee's
//! stub returns the return type its C prototype declares. The tile and the
//! position locals reach the compared scratchpad through their callees' stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3E6C));

pub const ignore_return = "the C falls off the end of this int function (no return statement): the result is the result register's leftover; its only caller (146C.c's func_800A3E6C(0)) ignores it";

pub const classes = [_][:0]const u8{ "busy", "mode-0-1", "mode-2", "mode-3", "mode-4-6", "mode-7" };

/// The return type a C prototype declares, for a stub to match.
fn Ret(comptime F: type) type {
    return @typeInfo(F).@"fn".return_type.?;
}

/// A value of the stub's return type: nothing for void, a drawn value otherwise.
fn done(ctx: *zd.Ctx, comptime R: type) R {
    if (R == void) return {} else return ctx.value(R);
}

pub const stubs = struct {
    pub fn func_800A38E0(ctx: *zd.Ctx, id: anytype) Ret(@TypeOf(c.func_800A38E0)) {
        const idx: usize = @intCast(id);
        // The C reads the actor's position after this call.
        if (ctx.value(u1) == 1) v.g.D_800F4538()[idx].?.unk0.position.vy = ctx.value(i16);
        if (ctx.value(u8) < 64) return -1; // the C's -1 test
        return ctx.value(u8) % 16; // a valid object index
    }

    pub fn func_800AE6C0(ctx: *zd.Ctx, actor: anytype, a: anytype, b: anytype) Ret(@TypeOf(c.func_800AE6C0)) {
        _ = a;
        _ = b;
        const p: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
        // The C reads 0x5AC back after this call (the |= store).
        if (ctx.value(u1) == 1) psx.byteAt(u32, p, 0x5AC).* = ctx.value(u32);
        if (ctx.value(u1) == 1) p.unk0.currentTileX = ctx.value(u8);
        return done(ctx, Ret(@TypeOf(c.func_800AE6C0)));
    }

    pub fn func_800A0204(ctx: *zd.Ctx, id: anytype, a: anytype, b: anytype, cc: anytype) Ret(@TypeOf(c.func_800A0204)) {
        _ = a;
        _ = b;
        _ = cc;
        const idx: usize = @intCast(id);
        if (ctx.value(u1) == 1) psx.byteAt(u32, v.g.D_800F4538()[idx].?, 0x5AC).* = ctx.value(u32);
        return done(ctx, Ret(@TypeOf(c.func_800A0204)));
    }

    pub fn func_800E4C1C(ctx: *zd.Ctx, x: anytype, y: anytype) Ret(@TypeOf(c.func_800E4C1C)) {
        _ = x;
        _ = y;
        if (ctx.value(u1) == 1) v.g.D_800E90A8().* = ctx.value(i32);
        return done(ctx, Ret(@TypeOf(c.func_800E4C1C)));
    }

    // The tile and the position are stack locals (the trace only tags their
    // address): the stubs copy what the C wrote into them to the scratchpad's
    // last 16 bytes, which the harness compares (the reviewer's change; the
    // position's pad is never written by the C, so it is not copied).
    pub fn func_800AA454(ctx: *zd.Ctx, id: anytype, tile: anytype, n: anytype) Ret(@TypeOf(c.func_800AA454)) {
        _ = n;
        const src: *align(1) const u32 = @ptrCast(tile);
        @as(*align(1) u32, @ptrFromInt(0x1F800FF8)).* = src.*;
        const idx: usize = @intCast(id);
        // The C reads the actor's tile back after this call.
        if (ctx.value(u1) == 1) v.g.D_800F4538()[idx].?.unk0.currentTileX = ctx.value(u8);
        return done(ctx, Ret(@TypeOf(c.func_800AA454)));
    }

    pub fn func_800AA620(ctx: *zd.Ctx, id: anytype, position: anytype, n: anytype) Ret(@TypeOf(c.func_800AA620)) {
        _ = id;
        _ = n;
        const p: *align(1) const [3]i16 = @ptrCast(position);
        @as(*align(1) [3]i16, @ptrFromInt(0x1F800FF0)).* = p.*;
        return done(ctx, Ret(@TypeOf(c.func_800AA620)));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const actor = ctx.alloc(v.D_800F4538_t);
    const object = ctx.alloc(v.D_800F45E0_t);
    for (v.g.D_800F4538()) |*slot| slot.* = actor;
    for (v.g.D_800F45E0()) |*slot| slot.* = object;

    // Busy bit on a fifth of the time; the rest of the word random.
    const w = psx.byteAt(u32, actor, 0x5AC);
    w.* = ctx.int(u32);
    if (ctx.chance(5)) {
        w.* |= 0x100000;
    } else {
        w.* &= ~@as(u32, 0x100000);
    }

    actor.unk0.position.vy = ctx.int(i16);
    actor.unk0.currentTileX = ctx.pick(u8, &.{ 0, 127, 128, 255 });
    actor.unk0.currentTileZ = ctx.pick(u8, &.{ 0, 127, 128, 255 });

    // Mode byte: the low three bits pick the arm, the rest is random.
    const m: u8 = ctx.range(u8, 0, 7);
    psx.byteAt(u8, object, 0x16C).* = m | (ctx.int(u8) & 0xF8);
    const table_arm = m <= 1;
    psx.byteAt(u8, object, 0x16E).* = if (table_arm) ctx.range(u8, 0, 23) else ctx.int(u8);
    const s: i32 = if (table_arm)
        ctx.range(i32, 0, 3)
    else
        ctx.pick(i32, &.{ 0, 1, 2, 3, -1, -2, 2147483647, -2147483648 });
    v.g.D_800F49E8().* = s;

    return .{id};
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    const actor = v.g.D_800F4538()[idx].?;
    if ((psx.byteAt(u32, actor, 0x5AC).* & 0x100000) != 0) return 0;
    const mode: u8 = psx.byteAt(u8, v.g.D_800F45E0()[0].?, 0x16C).* & 7;
    return switch (mode) {
        0, 1 => 1,
        2 => 2,
        3 => 3,
        4, 5, 6 => 4,
        7 => 5,
        else => unreachable,
    };
}
