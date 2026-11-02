//! zig_diff spec: func_800D5A98 -> func_800D5A98.func_800D5A98 (5BF94.c).
//! Inputs: the script's unk30 block null or set, its frame before, at or
//! after the event's; the spawn state's index before, at or past the
//! P-file's target count (0..3), its delay 0 or not, the event's delay 0 or
//! 1 (one spawn or a run); the targets' kinds 4 or not and their actor
//! byte any (it indexes the event's six actors unchecked: a 256-byte
//! event); the script's unk14 bits 11..15 0 or not (the final spawn).
//! func_800CE83C gets the C's stack spawn record (a tag): its stub logs the
//! fields the C sets and returns an arena child.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5A98));

pub const classes = [_][:0]const u8{ "no-block", "early", "spawning", "final", "done" };

var log: ?[*][9]u32 = null;
var child: ?*v.D_800F53B8_t = null;

pub const stubs = struct {
    pub fn func_800CE83C(ctx: *zd.Ctx, spawn: anytype) ?*v.D_800F53B8_t {
        const s: *v.D_800F53B8_t2 = @ptrCast(@alignCast(spawn));
        const k = @min(@as(usize, ctx.calls), 7);
        log.?[k] = .{ s.unk0.raw, s.unk4.raw, s.unk8.raw, s.unkC.raw, @bitCast(s.unk10), s.unk14.raw, @bitCast(s.unk18), s.unk1C.raw, s.unk20 };
        return child;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(8 * 36, 16)));
    child = ctx.alloc(v.D_800F53B8_t);
    const s = ctx.alloc(v.D_800F53B8_t);
    const event = ctx.allocBytes(256, 16);
    event[7] = ctx.pick(u8, &.{ 0, 5, 10 });
    event[8] = ctx.pick(u8, &.{ 0, 0, 1 });
    if (ctx.chance(6)) {
        psx.put(&s.unkD1C.unk30, null);
    } else {
        const t4 = ctx.alloc(v.D_800F53B8_t4);
        t4.unk2 = ctx.pick(i16, &.{ 4, 5, 6, 10, -1 });
        psx.put(&s.unkD1C.unk30, t4);
    }
    const n = ctx.range(u8, 0, 3);
    const targets = ctx.allocBytes(16 + 12 * 4, 16);
    targets[2] = n;
    for (0..4) |i| @as(*align(1) u16, @ptrCast(targets + 16 + 12 * i)).* = ctx.pick(u16, &.{ 4, 4, 5 });
    const pf = ctx.alloc(v.D_800F569C_t);
    psx.put(&pf.unkD0, @as(*anyopaque, @ptrCast(targets)));
    v.g.D_800F569C().* = pf;
    const state = ctx.alloc(v.timedSpawnState);
    psx.put(&state.data, @as(*u8, @ptrCast(event)));
    state.index = switch (ctx.range(u8, 0, 3)) {
        0 => 0,
        1 => @as(i16, n) - 1,
        2 => n,
        else => @as(i16, n) + 1,
    };
    if (state.index < 0) state.index = 0;
    state.delay = ctx.pick(i16, &.{ 0, 0, 1, 2 });
    if (ctx.random.boolean()) psx.byteAt(u32, s, 0x14).* &= ~@as(u32, 0xF800);
    return .{ s, state, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const block = psx.get(args[0].?.unkD1C.unk30) orelse return 0;
    const event: [*]u8 = @ptrCast(psx.get(args[1].?.data).?);
    if (event[7] > block.unk2) return 1;
    const targets: [*]u8 = @ptrCast(psx.get(v.g.D_800F569C().*.?.unkD0).?);
    if (args[1].?.index < targets[2]) return 2;
    return if (args[1].?.index == targets[2]) 3 else 4;
}
