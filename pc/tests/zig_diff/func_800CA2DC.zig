//! zig_diff spec: func_800CA2DC -> func_800CA2DC.func_800CA2DC (5BF94.c).
//! Inputs: the player's actor (vs_battle_characterState's unk3C, arena
//! blocks) with RISK 0 or not; the battle-mode notice D_800EB9CC 0, 1 or 2
//! and its step 0..5; the HUD slide D_800EB9AF, D_800EB9B0 (0, a step,
//! 0x200000: hidden) and D_800EB9AE; text box 7 open or not and the lower
//! screen's state; the status icon slots D_800EB9D0 (0xFF empty, a status,
//! the scroll counter 12 and its neighbours) and vs_battle_getStatusFlags'
//! flags none, one, two or many; the tick speed 4 or not. The sprite stubs
//! return one arena packet; vs_battle_getLimbStatus returns 0..3; the
//! "RISK  ZERO" text is compared by its bytes (textArgument).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CA2DC));

pub const classes = [_][:0]const u8{ "hidden", "no-icons", "icons" };

var packet: ?[*]u32 = null;
var status: c_int = 0;

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "vs_battle_renderTextRaw");
}

pub const stubs = struct {
    pub fn vs_battle_setSprite(ctx: *zd.Ctx, a: anytype, b: anytype, w: anytype, ot: anytype) [*c]u32 {
        _ = ctx;
        _ = a;
        _ = b;
        _ = w;
        _ = ot;
        return packet.?;
    }
    pub fn vs_battle_setSpriteDefaultTexPage(ctx: *zd.Ctx, a: anytype, b: anytype, w: anytype, ot: anytype) [*c]u32 {
        _ = ctx;
        _ = a;
        _ = b;
        _ = w;
        _ = ot;
        return packet.?;
    }
    pub fn vs_battle_getLimbStatus(ctx: *zd.Ctx, limb: anytype) c_int {
        _ = limb;
        return ctx.value(u2);
    }
    pub fn vs_battle_getStatusFlags(ctx: *zd.Ctx, actor: anytype) c_int {
        _ = ctx;
        _ = actor;
        return status;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    packet = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    const front = ctx.allocBytes(64, 16) + 32;
    psx.put(&v.g.vs_scratch().unk4, @as(*anyopaque, @ptrCast(front)));
    const player = ctx.alloc(v.vs_battle_actor);
    const actor = ctx.alloc(v.vs_battle_actor2);
    actor.risk = ctx.edge(u16, &.{ 0, 0, 1, 100 });
    psx.put(&player.stats, actor);
    v.g.vs_battle_characterState().* = player;
    v.g.D_800EB9CC().* = ctx.edge(u8, &.{ 0, 1, 1, 2 });
    v.g.D_800EB9CD().* = ctx.edge(u8, &.{ 0, 1, 3, 4, 5 });
    v.g.D_800EB9AF().* = ctx.edge(u8, &.{ 0, 0, 0, 1 });
    v.g.D_800EB9B0().* = ctx.edge(i32, &.{ 0, 0, 0x80000, 0x200000 });
    v.g.D_800EB9AE().* = ctx.edge(u8, &.{ 0, 0, 0, 1 });
    const box7 = &v.g.textBoxes()[7];
    box7.state = switch (ctx.range(u8, 0, 2)) {
        0 => 0,
        1 => @as(i16, box7.unk22) + 1,
        else => ctx.int(i16),
    };
    v.g.vs_battle_lowerScreenUiState().* = ctx.edge(u8, &.{ 0, 1, 2 });
    const slots = &v.g.D_800EB9D0().u8;
    for (0..3) |i| slots[i] = ctx.edge(u8, &.{ 0xFF, 0, 1, 5, 31, 40 });
    slots[3] = ctx.edge(u8, &.{ 0, 2, 3, 8, 9, 11, 12, 13, 14, 15 });
    status = switch (ctx.range(u8, 0, 4)) {
        0 => 0,
        1 => @as(c_int, 1) << @intCast(ctx.edge(u5, &.{ 0, 1, 5, 31 })),
        2 => 0x22,
        3 => 0x0FFF,
        else => ctx.int(c_int),
    };
    if (ctx.chance(4)) status |= @as(c_int, 1) << @intCast(slots[1] & 31);
    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ 2, 4, 4 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800EB9AF().* != 0 and v.g.D_800EB9B0().* == 0x200000) return 0;
    if (v.g.D_800EB9AE().* != 0 or status == 0) return 1;
    return 2;
}
