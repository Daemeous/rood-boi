//! zig_diff spec: func_80069D14 -> func_80069D14.func_80069D14 (146C.c).
//! Inputs: all 16 slots of vs_battle_actors are rewritten each iteration. A
//! random mode picks the table's shape: empty (every slot null), no-match
//! (live actors, none with unk8 == 0x80 and unk9 == 0x18), or match (a random
//! slot, including slot 15, holds a matching actor). Live actors take unk8 and
//! unk9 from edge sets around 0x80 and 0x18, and some slots are null even in
//! the populated modes, so the null guard is taken both ways.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80069D14));

pub const classes = [_][:0]const u8{ "empty", "no-match", "match" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slots = v.g.vs_battle_actors();
    const mode = ctx.range(u8, 0, 9);
    for (0..16) |i| {
        if (mode == 0 or ctx.chance(3)) {
            slots[i] = null;
            continue;
        }
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.unk8 = ctx.pick(u8, &.{ 0x80, 0x00, 0x7F, 0x81, 0xFF, 0x08 });
        actor.unk9 = ctx.pick(u8, &.{ 0x18, 0x17, 0x19, 0x00, 0xFF });
        if (mode == 1 and actor.unk8 == 0x80 and actor.unk9 == 0x18) actor.unk9 = 0x17;
        slots[i] = actor;
    }
    if (mode >= 2) {
        const j = ctx.range(usize, 0, 15);
        const actor = slots[j] orelse ctx.alloc(v.vs_battle_actor);
        actor.unk8 = 0x80;
        actor.unk9 = 0x18;
        slots[j] = actor;
    }
    return .{};
}

pub fn classify(_: Args) usize {
    const slots = v.g.vs_battle_actors();
    var any = false;
    var hit = false;
    for (0..16) |i| {
        const a = slots[i] orelse continue;
        any = true;
        if (a.unk8 == 0x80 and a.unk9 == 0x18) hit = true;
    }
    if (hit) return 2;
    return if (any) 1 else 0;
}
