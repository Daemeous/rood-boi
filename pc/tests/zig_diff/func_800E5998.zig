//! zig_diff spec: func_800E5998 -> func_800E5998.func_800E5998 (7CD68.c).
//! Inputs: a holder object whose `next` links up to five list actors (or none),
//! with ids 0-7 indexing a table of func objects whose unk16 is 0x10 about half
//! the time and otherwise a few edge values. Each actor's unk30_0 is a 4-bit key
//! in 0-4 and its unk30_9 a 5-bit key that is usually 0-4 and sometimes any 5-bit
//! value (16-31 can never match, but `& 15` would). The C calls nothing, so there
//! are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E5998));

pub const classes = [_][:0]const u8{ "empty", "no-trigger", "trigger-no-match", "trigger-match" };

var holder: *v.vs_battle_actor = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    holder = ctx.alloc(v.vs_battle_actor);
    v.g.vs_battle_actors()[0] = holder;

    for (0..8) |i| {
        const f = ctx.alloc(v.func_800DEEA4_t2);
        f.unk16 = if (ctx.chance(2)) @as(i8, 0x10) else ctx.pick(i8, &.{ 0, 0x10, 0x11, -1, 127, -128 });
        v.g.D_800F5878()[i] = f;
    }

    const n = ctx.range(usize, 0, 5);
    var list: [5]*v.vs_battle_actor = undefined;
    for (0..n) |i| {
        const a = ctx.alloc(v.vs_battle_actor);
        a.id = ctx.range(i32, 0, 7);
        psx.setBits(a, "unk30_0", ctx.range(u4, 0, 4));
        psx.setBits(a, "unk30_9", if (ctx.chance(3)) ctx.int(u5) else ctx.range(u5, 0, 4));
        list[i] = a;
    }
    psx.put(&holder.next, if (n > 0) list[0] else null);
    for (0..n) |i| {
        psx.put(&list[i].next, if (i + 1 < n) list[i + 1] else null);
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const head = psx.get(holder.next) orelse return 0;
    var trigger = false;
    var a: ?*v.vs_battle_actor = head;
    while (a) |act| : (a = psx.get(act.next)) {
        const f = v.g.D_800F5878()[@intCast(act.id)].?;
        if (f.unk16 != 0x10) continue;
        trigger = true;
        var b: ?*v.vs_battle_actor = head;
        while (b) |o| : (b = psx.get(o.next)) {
            if (psx.getBits(o, "unk30_0") == psx.getBits(act, "unk30_9")) return 3;
        }
    }
    return if (trigger) 2 else 1;
}
