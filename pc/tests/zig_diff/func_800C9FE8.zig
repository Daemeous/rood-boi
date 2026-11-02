//! zig_diff spec: func_800C9FE8 -> func_800C9FE8.func_800C9FE8 (5BF94.c).
//! Inputs: the slide flag D_800EBC78 set (the step goes back, from 0 too)
//! or not (the step follows vs_battle_animationIndices, whose entry may be
//! the hidden 10); the step 0, 1, 9, 10 or 11; the slot labels (ROOD_PC's
//! pc_weapon_slots_label, a stub) 0 or not per slot. The sprite stub returns
//! one arena packet (its two words per sprite: the state hash sees them);
//! pc_classic_site gets the C's stack record (a tag): its stub logs it.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C9FE8));

pub const classes = [_][:0]const u8{ "hidden", "shown" };

var packet: ?[*]u32 = null;
var log: ?[*][4]c_int = null;

pub const stubs = struct {
    pub fn vs_battle_setSpriteDefaultTexPage(ctx: *zd.Ctx, a: anytype, b: anytype, w: anytype, ot: anytype) [*c]u32 {
        _ = ctx;
        _ = a;
        _ = b;
        _ = w;
        _ = ot;
        return packet.?;
    }
    pub fn pc_weapon_slots_label(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return if (ctx.value(u8) < 100) 0 else ctx.value(c_int);
    }
    pub fn pc_classic_site(ctx: *zd.Ctx, kind: anytype, record: anytype, x: anytype, y: anytype, first: anytype, end: anytype) void {
        _ = kind;
        _ = x;
        _ = y;
        _ = first;
        _ = end;
        const r: *c.pc_classic_ability_icons = @ptrCast(@alignCast(record));
        log.?[@min(ctx.calls, 31)] = .{ r.animation, r.offset, r.group, r.availability };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    packet = @ptrCast(@alignCast(ctx.allocBytes(32, 16)));
    log = @ptrCast(@alignCast(ctx.allocBytes(32 * 16, 16)));
    psx.put(&v.g.vs_scratch().unk8, @as(*anyopaque, @ptrCast(ctx.allocBytes(32, 16))));
    v.g.D_800EBC78().* = ctx.edge(u8, &.{ 0, 1 });
    const step = ctx.edge(u8, &.{ 0, 1, 9, 10, 11 });
    v.g.D_800EBCE8().* = step;
    if (ctx.chance(4)) v.g.vs_battle_animationIndices()[step] = 10;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const step = v.g.D_800EBCE8().*;
    const next = if (v.g.D_800EBC78().* != 0) (if (step != 0) step - 1 else 0) else v.g.vs_battle_animationIndices()[step];
    return if (next == 10) 0 else 1;
}
