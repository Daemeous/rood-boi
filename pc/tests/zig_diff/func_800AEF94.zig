//! zig_diff spec: func_800AEF94 -> func_800AEF94.func_800AEF94 (44F14.c).
//! Inputs: the 17 actor slots D_800F4538 hold one of five arena actors (a
//! slot may repeat one) or null; each actor's skeleton count, flag word at 8
//! (hidden 1, fading 4, 0xF00, attached 0x2000, bit 31), unk13 (0xFC: off;
//! else a bone 0..41), unk12 (the parent slot 0..16, empty or not), its fade
//! (unk17E4/unk17E8: mode 1 or not, frames 0..9), texture id 0..2 (shared
//! ones skip the load), 0x5AC & 3, 0x5B0 bit 3, linked actor unk17FD (0, 1,
//! or a live slot), unk6F4 offsets, unk63C around 0x80 and unk5CC. Three
//! weapons in the 20 slots (flag 0x10, bone, actor nibble) and three room
//! objects in the 16 (alive or not, unk1A 0xF7, the selected slot
//! D_800F4B19 with D_800F4B18 0 or 1, the model height at unk68 + 0x50).
//! The tick speed is 2, 4 or other. The block-scope statics `zero` and the
//! log-once latch `reported` are the C's own (globalised: views
//! func_800AEF94__zero, func_800AEF94__reported); the log's line is compared
//! by its text (textArgument). Every callee is a recording stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AEF94));

pub const classes = [_][:0]const u8{ "empty-parent", "linked", "other" };

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_log");
}

fn flags(p: anytype) *align(1) u32 {
    return psx.byteAt(u32, p, 8);
}

fn live(a: *v.D_800F4538_t) bool {
    return a.unk0.nBones != 0 and flags(a).* & 1 == 0 and a.unk0.unk13 != 0xFC;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.D_800F4538();
    var actors: [5]*v.D_800F4538_t = undefined;
    for (&actors) |*a| a.* = ctx.alloc(v.D_800F4538_t);
    for (slots) |*s| s.* = if (ctx.chance(3)) null else actors[ctx.range(usize, 0, 4)];

    for (actors) |a| {
        a.unk0.nBones = if (ctx.chance(8)) 0 else ctx.range(u8, 1, 42);
        var f = ctx.int(u32) & 0x8000_3F07;
        if (ctx.chance(2)) f &= ~@as(u32, 1);
        if (ctx.chance(2)) f |= 4;
        f &= if (ctx.chance(2)) ~@as(u32, 0x2000) else ~@as(u32, 0);
        flags(a).* = f;
        a.unk0.unk13 = if (ctx.chance(8)) 0xFC else ctx.range(u8, 0, 41);
        a.unk0.unk12 = ctx.range(u8, 0, 16);
        a.unk0.unk64.unk0 = ctx.edge(u8, &.{ 0, 1 });
        a.unk17E4.unk0 = ctx.edge(u8, &.{ 1, 1, 0, 2 });
        a.unk17E4.unk3 = ctx.range(u8, 0, 9);
        a.unk17E4.unk1 = ctx.int(u8);
        a.unk17E4.unk2 = ctx.int(u8);
        a.unk17E8.unk1 = if (ctx.chance(3)) a.unk17E4.unk1 else ctx.int(u8);
        a.unk17E8.unk2 = if (ctx.chance(3)) a.unk17E4.unk2 else ctx.int(u8);
        a.unk6E6 = ctx.range(u16, 0, 2);
        a.unk5B2 = ctx.int(u8);
        a.unk63C = ctx.pick(u16, &.{ 0x7F, 0x80, 0x81, 0 });
        a.unk5CC = ctx.edge(u8, &.{ 0, 1, 0xFF });
    }
    // A linked actor is a live slot (the C reads it without a test).
    for (actors) |a| {
        const id = ctx.range(u8, 0, 16);
        a.unk17FD = if (ctx.chance(3)) 0 else if (ctx.chance(4)) 1 else if (slots[id] != null) id else 0;
    }

    const weapons = v.g.vs_battle_wepModels();
    for (weapons) |*w| w.* = null;
    for (0..3) |_| {
        const w = ctx.alloc(v.vs_battle_wepModels_t);
        flags(w).* = ctx.int(u32);
        w.unkD = if (ctx.chance(4)) 0 else ctx.range(u8, 1, 41);
        weapons[ctx.range(usize, 0, 19)] = w;
    }

    const objects = v.g.D_800F45E0();
    for (objects) |*o| o.* = null;
    for (0..3) |_| {
        const o = ctx.alloc(v.D_800F45E0_t);
        const model = ctx.allocBytes(0x60, 16);
        psx.put(&o.unk68, @as(*anyopaque, @ptrCast(model)));
        o.unk0 = ctx.edge(u8, &.{ 0, 1, 1 });
        o.unk1A = ctx.pick(i16, &.{ 0xF7, 0xF7, 0xF6 });
        o.unk64.unk0 = ctx.edge(u8, &.{ 0, 1 });
        objects[ctx.range(usize, 0, 15)] = o;
    }
    v.g.D_800F4B19().* = ctx.range(u8, 0, 16);
    v.g.D_800F4B18().* = ctx.edge(u8, &.{ 0, 0, 1 });
    v.g.vs_gametime_tickspeed().* = ctx.pick(i32, &.{ 2, 4, 4, 1, 8 });
    v.g.func_800AEF94__reported().* = if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1 });
    return .{@ptrCast(ctx.alloc(v.MATRIX))};
}

pub fn classify(args: Args) usize {
    _ = args;
    const slots = v.g.D_800F4538();
    var linked = false;
    for (slots) |s| {
        const a = s orelse continue;
        if (!live(a)) continue;
        if (flags(a).* & 0x2000 != 0) {
            if (slots[a.unk0.unk12] == null) return 0;
        } else if (a.unk17FD >= 2) {
            linked = true;
        }
    }
    return if (linked) 1 else 2;
}
