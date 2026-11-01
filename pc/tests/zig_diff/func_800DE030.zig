//! zig_diff spec: func_800DE030 -> func_800DE030.func_800DE030 (74088.c).
//! Inputs: `elapsed` around the /240 and /180 steps (negative too); a chain
//! of 0-3 actors from vs_battle_actors[0], each with stats and an id 0-17
//! (D_800F5878's room); the clock D_800F58BC->unk18 placed so that after the
//! step it is at, below or above 2880 and at a status timer's 30x duration;
//! limb HP at the +2 clamp and around 2 for the two key limbs (unk34 and
//! unk35, 0-7); HP and MP at their clamps; poison (0x4000) and regen
//! (0x20000) with intervals 1-60 and maxHP 0 / small (the drain lands on 0);
//! D_80069BBC's five masks single bits (the sign bit included). The
//! candidate refresh stub sometimes moves the clock, which the next actor
//! must re-read.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DE030));

pub const classes = [_][:0]const u8{ "no-actors", "poison", "regen", "plain" };

pub const stubs = struct {
    pub fn func_800E4F14(ctx: *zd.Ctx, state: anytype) void {
        _ = state;
        if (ctx.value(u8) < 64) v.g.D_800F58BC().*.?.unk18 = ctx.value(u16);
    }
};

fn table() *v.statusEffectTable {
    return @ptrCast(v.g.vs_main_statusEffectParams());
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const elapsed: c_int = ctx.pick(c_int, &.{ 0, 1, -1, 31, 32, 179, 180, 181, 239, 240, 241, 480, 512, 1000, -240, 32767 });
    const clock = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = clock;
    const timer: u8 = ctx.int(u8);
    const final: u16 = switch (ctx.range(u8, 0, 4)) {
        0 => 2880,
        1 => 2881,
        2 => 2879,
        3 => @as(u16, timer) * 30,
        else => ctx.int(u16),
    };
    clock.unk18 = final -% psx.conv(u16, elapsed);

    psx.at(&table().entries, 14).interval = ctx.range(u16, 1, 60);
    psx.at(&table().entries, 17).interval = ctx.range(u16, 1, 60);
    var m: usize = 0;
    while (m < 5) : (m += 1) v.g.D_80069BBC()[m] = @as(u32, 1) << ctx.int(u5);

    const count = if (ctx.chance(8)) 0 else ctx.range(usize, 1, 3);
    var first: ?*v.vs_battle_actor = null;
    var k: usize = 0;
    while (k < count) : (k += 1) {
        const actor = ctx.alloc(v.vs_battle_actor);
        const stats = ctx.alloc(v.vs_battle_actor2);
        psx.put(&actor.next, first);
        psx.put(&actor.stats, stats);
        actor.id = ctx.range(c_int, 0, 17);
        v.g.D_800F5878()[@intCast(actor.id)] = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
        first = actor;

        var limb: usize = 0;
        while (limb < 6) : (limb += 1) {
            const l = &stats.limbs[limb];
            l.maxHp = ctx.pick(i16, &.{ 0, 1, 2, 3, 100, 32767 });
            l.hp = switch (ctx.range(u8, 0, 3)) {
                0 => l.maxHp -% 2,
                1 => l.maxHp -% 1,
                2 => ctx.range(i16, 0, 4),
                else => ctx.int(i16),
            };
        }
        psx.setBits(stats, "unk34", ctx.range(u8, 0, 7));
        psx.setBits(stats, "unk35", ctx.range(u8, 0, 7));

        stats.statuses = ctx.int(i32);
        if (ctx.chance(2)) stats.statuses |= @bitCast(v.g.D_80069BBC()[ctx.range(usize, 0, 4)] |
            if (ctx.chance(2)) @as(u32, 0x4000) else 0x20000);
        stats.maxHP = ctx.pick(i16, &.{ 0, 1, 10, 100, 999, 32767 });
        stats.currentHP = ctx.pick(i16, &.{ 0, 1, stats.maxHP, stats.maxHP -% 1, stats.maxHP -% 2, stats.maxHP -% 3, -1 });
        stats.maxMP = ctx.pick(i16, &.{ 0, 1, 10, 100, 32767 });
        stats.currentMP = ctx.pick(i16, &.{ 0, stats.maxMP, stats.maxMP -% 1, stats.maxMP -% 2, stats.maxMP -% 3, -1 });
        var t: usize = 0;
        while (t < 8) : (t += 1) stats.statusTimers[t] = if (ctx.chance(3)) timer else ctx.int(u8);
    }
    v.g.vs_battle_actors()[0] = first;
    return .{elapsed};
}

pub fn classify(args: Args) usize {
    _ = args;
    const a = v.g.vs_battle_actors()[0] orelse return 0;
    const statuses = psx.get(a.stats).?.statuses;
    if (statuses & 0x4000 != 0) return 1;
    if (statuses & 0x20000 != 0) return 2;
    return 3;
}
