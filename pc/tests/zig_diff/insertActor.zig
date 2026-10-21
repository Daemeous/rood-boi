//! zig_diff spec: _insertActor -> insert_actor.insertActor (146C.c).
//! Inputs: an actor in the arena whose allegiance's low three bits are clear
//! or set (the high bit both ways) and whose id is zero or not (1, -1,
//! 0x7FFFFFFF, INT_MIN too). The 16-slot list has null slots (never slot 0,
//! which the insert reads), slots whose next is null or another arena object,
//! the actor itself in slot 0 (the copy-from-self case), and the actor linked
//! from a random slot, slot 15 included (the early return).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").insertActor));

pub const classes = [_][:0]const u8{
    "allegiance-low-clear",
    "id-zero",
    "already-listed",
    "self-in-slot-0",
    "inserted",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_actor);
    actor.allegiance = if (ctx.chance(4))
        ctx.int(u16) & 0xFFF8
    else
        (ctx.int(u16) & 0xFFF8) | ctx.range(u16, 1, 7);
    actor.id = if (ctx.chance(6)) 0 else ctx.pick(i32, &.{ 1, -1, 0x7FFF_FFFF, -0x8000_0000 });

    const slots = v.g.vs_battle_actors();
    const self_in_slot0 = ctx.chance(8);
    for (0..16) |i| {
        if (i == 0 and self_in_slot0) {
            slots[0] = actor;
        } else if (i != 0 and ctx.chance(3)) {
            slots[i] = null;
        } else {
            slots[i] = ctx.alloc(v.vs_battle_actor);
        }
    }
    for (0..16) |i| {
        const s = slots[i] orelse continue;
        const other = ctx.alloc(v.vs_battle_actor);
        psx.put(&s.next, if (ctx.chance(2)) null else other);
    }
    if (ctx.chance(3)) {
        const m = ctx.range(usize, 0, 15);
        if (slots[m] == null) slots[m] = ctx.alloc(v.vs_battle_actor);
        psx.put(&slots[m].?.next, actor);
    }
    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if ((actor.allegiance & 7) == 0) return 0;
    if (actor.id == 0) return 1;
    const slots = v.g.vs_battle_actors();
    for (0..16) |i| {
        const s = slots[i] orelse continue;
        if (psx.get(s.next) == actor) return 2;
    }
    return if (slots[0].? == actor) 3 else 4;
}
