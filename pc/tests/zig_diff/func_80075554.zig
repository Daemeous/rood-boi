//! zig_diff spec: func_80075554 -> actor_commands.func_80075554 (146C.c, actor_commands.inc).
//! Inputs: no arguments. One actor in a random slot 0..15 (the others empty; sometimes an
//! inert second) whose scenario is picked per iteration: the slot table empty; the status
//! bit 0x1000000 (skip); an allegiance outside 2 and 4 with BATTLE's mode not 3 (skip); a
//! command in progress (state 0x40) with its command in each of the C's groups; another
//! state; and a new command (state 0x80) for each case of the C's switch, 1 and the unlisted
//! ones included. The actor's aim index unkA is below or at 16, the stats' kind bytes sit on
//! the speed tests (unk32 2, 0x40, 4, 0x80, 0x10, 0x20), the camera and effect bits take every
//! value, and the stubs give the status bits, the positions, the vectors and func_800751B8's
//! result (a nonzero value ends the pass with it). func_800A1108, func_800753F8 and
//! VectorNormalSS fill their out-parameters from stub values, so no local is uninitialised.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80075554));

const cmds = [_]u8{ 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 1 };

pub const classes = blk: {
    const fixed = [_][:0]const u8{ "empty", "status-skip", "allegiance-skip", "running-group", "running-13", "running-23", "running-20-22", "running-default", "other-state" };
    var out: [fixed.len + cmds.len + 1][:0]const u8 = undefined;
    for (fixed, 0..) |name, k| out[k] = name;
    for (cmds, 0..) |n, k| out[fixed.len + k] = std.fmt.comptimePrint("cmd-{d}", .{n});
    out[fixed.len + cmds.len] = "cmd-unlisted";
    break :blk out;
};

var scenario: usize = 0;
var status: [17]c_uint = @splat(0);
var tail_result: c_int = 0;

pub const stubs = struct {
    pub fn func_800A0BE0(ctx: *zd.Ctx, actor_id: c_int) c_uint {
        _ = ctx;
        return status[@as(usize, @as(c_uint, @bitCast(actor_id))) % 17];
    }
    pub fn func_800A1108(ctx: *zd.Ctx, index: c_int, out: ?*anyopaque) c_int {
        // sometimes the camera and effect bits of the actor asked about change under the call (the C reads
        // them after its first call, so a port that read them before it takes the old ones)
        if (index >= 0 and index < 16 and ctx.chance(3)) {
            if (v.g.vs_battle_actors()[@intCast(index)]) |a| a.unkB = (a.unkB & 0xF0) | ctx.value(u4);
        }
        const p: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        p.unk0.unk0.value = ctx.value(i32);
        p.unk0.unk4.vx = ctx.value(i16);
        p.unk0.unk4.vy = ctx.value(i16);
        p.unk0.unk4.vz = ctx.value(i16);
        p.unk0.unk4.pad = ctx.value(i16);
        p.unkC = ctx.value(i32);
        return ctx.value(c_int);
    }
    /// The vector handed over is a stack local: copy what the C has set (x and y; z too when `with_z`) where
    /// the harness compares it.
    fn seen(ctx: *zd.Ctx, vector: [*c]v.SVECTOR, with_z: bool) void {
        if (vector == null) return;
        const copy = ctx.alloc(v.SVECTOR);
        copy.vx = vector[0].vx;
        copy.vy = vector[0].vy;
        if (with_z) copy.vz = vector[0].vz;
    }
    pub fn func_8009FC60(ctx: *zd.Ctx, actor: c_int, mode: c_int, vector: [*c]v.SVECTOR, extra: c_int) c_int {
        _ = actor;
        _ = mode;
        _ = extra;
        seen(ctx, vector, false); // commands 4, 6 and 7 leave z unset
        return ctx.value(c_int);
    }
    pub fn func_800B6500(ctx: *zd.Ctx, actor: c_int, vector: [*c]v.SVECTOR) void {
        _ = actor;
        seen(ctx, vector, true);
    }
    pub fn func_800B6530(ctx: *zd.Ctx, actor: c_int, vector: [*c]v.SVECTOR, angle: c_int) void {
        _ = actor;
        _ = angle;
        seen(ctx, vector, true);
    }
    pub fn func_800B653C(ctx: *zd.Ctx, actor: c_int, vector: [*c]v.SVECTOR, speed: c_int, angle: c_int) void {
        _ = actor;
        _ = speed;
        _ = angle;
        seen(ctx, vector, true);
    }
    pub fn func_800753F8(ctx: *zd.Ctx, actor: ?*anyopaque, out: [*c]v.SVECTOR) void {
        _ = actor;
        out[0].vx = ctx.value(i16);
        out[0].vy = ctx.value(i16);
        out[0].vz = ctx.value(i16);
    }
    pub fn VectorNormalSS(ctx: *zd.Ctx, in: [*c]v.SVECTOR, out: [*c]v.SVECTOR) c_int {
        _ = in;
        out[0].vx = ctx.value(i16);
        out[0].vy = ctx.value(i16);
        out[0].vz = ctx.value(i16);
        return ctx.value(c_int);
    }
    pub fn func_800751B8(ctx: *zd.Ctx, index: c_int, position: ?*anyopaque) c_int {
        _ = ctx;
        _ = index;
        _ = position;
        return tail_result;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    scenario = ctx.range(usize, 0, classes.len - 1);
    const table = v.g.vs_battle_actors();
    var k: usize = 0;
    while (k < 17) : (k += 1) table[k] = null;
    v.g.D_800F196C().* = if (scenario == 2) ctx.edge(u32, &.{ 2, 1, 0 }) else ctx.edge(u32, &.{ 2, 2, 2, 3 });
    for (&status) |*s| s.* = ctx.edge(c_uint, &.{ 0, 0x4000, 0x4000, 0x12000000, 0x2000000, 0x10000000, 0x4000000, 0x200000, 0x204000, 0xFFFF_FFFF & ~@as(c_uint, 0x1000000) });
    tail_result = ctx.edge(c_int, &.{ 0, 0, 0, 0, 5, -1, 0x7FFFFFFF });
    const block = ctx.alloc(v.D_800F19CC_t);
    block.unk298C = ctx.edge(u8, &.{ 0xFF, 0xFF, 0, 1, 0xFE });
    v.g.D_800F19CC().* = block;
    if (scenario == 0) return .{};

    const slot = ctx.range(usize, 0, 15);
    const actor = ctx.alloc(v.vs_battle_actor);
    table[slot] = actor;
    if (ctx.chance(4)) {
        const other = ctx.alloc(v.vs_battle_actor);
        other.allegiance = 4;
        other.unk8 = ctx.edge(u8, &.{ 0, 1, 0x41 });
        const at = (slot + 1 + ctx.range(usize, 0, 13)) % 16;
        table[at] = other;
        status[at] &= ~@as(c_uint, 0x1000000);
    }
    status[slot] = if (scenario == 1) status[slot] | 0x1000000 else status[slot] & ~@as(c_uint, 0x1000000);
    actor.id = ctx.pick(i32, &.{ 0, 1, 16, -1 });
    actor.allegiance = if (scenario == 2) ctx.edge(u16, &.{ 0, 1, 3, 5, 0xFFFF, 6 }) else if (v.g.D_800F196C().* == 3 and ctx.chance(2)) ctx.int(u16) else ctx.edge(u16, &.{ 4, 2 });
    actor.unkA = ctx.edge(u8, &.{ 0, 15, 16, 17, 255, 3 });
    actor.unkB = ctx.int(u8);
    actor.unkC = ctx.pick(i32, &.{ 0, 1, -1, 0x1000, -0x1000, 0x7FFF, -0x8000, 0x7FFFFFFF, -0x80000000, 0x12345678 });
    actor.unk10.s32 = ctx.pick(i32, &.{ 0, 1, -1, 0x1000, 0x7FFFFFFF, -0x80000000 });
    actor.unk14 = ctx.pick(i32, &.{ 0, 1, -1, 0x1000, 0x7FFFFFFF, -0x80000000 });
    const stats = ctx.alloc(v.vs_battle_actor2);
    stats.unk31 = ctx.pick(u8, &.{ 0, 1, 255 });
    stats.unk32 = ctx.edge(u8, &.{ 0, 2, 0x40, 4, 0x80, 0x10, 0x20, 1 });
    stats.unk33 = ctx.pick(u8, &.{ 0, 1, 255 });
    // command 24 reads armor[unkC[0]][unkC[1]].id: zero (the id becomes 1) as often as not
    for (&stats.armor) |*row| for (row) |*entry| {
        entry.id = ctx.edge(i16, &.{ 0, 0, 1, -1, 0x7FFF });
    };
    psx.put(&actor.stats, stats);
    psx.put(&actor.unk44, if (ctx.chance(3)) null else ctx.alloc(v.D_800F4538_unk0));

    var command: u8 = undefined;
    switch (scenario) {
        1, 2 => {
            actor.unk8 = ctx.edge(u8, &.{ 0x40, 0x80 });
            command = ctx.int(u8);
        },
        3 => {
            actor.unk8 = 0x40;
            command = ctx.edge(u8, &.{ 9, 10, 11, 12, 14, 15, 18 });
        },
        4 => {
            actor.unk8 = 0x40;
            command = 13;
        },
        5 => {
            actor.unk8 = 0x40;
            command = 23;
        },
        6 => {
            actor.unk8 = 0x40;
            command = ctx.edge(u8, &.{ 20, 21, 22 });
        },
        7 => {
            actor.unk8 = 0x40;
            command = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 17, 19, 24, 25, 255 });
        },
        8 => {
            actor.unk8 = ctx.edge(u8, &.{ 0, 1, 0x41, 0x7F, 0x81, 0xFF, 0x20 });
            command = ctx.int(u8);
        },
        else => {
            actor.unk8 = 0x80;
            const n = scenario - 9;
            command = if (n < cmds.len) cmds[n] else ctx.edge(u8, &.{ 0, 5, 25, 26, 255 });
        },
    }
    actor.unk9 = command;
    if (command == 13 and ctx.chance(2)) {
        // nothing left of the movement: the busy test's all-zero edge
        actor.unkC = 0;
        actor.unk10.s32 = 0;
        status[slot] &= ~@as(c_uint, 0x4000);
    }
    if (command == 8 and slot > 0 and ctx.chance(2)) {
        // an earlier actor's command 19 leaves its vector behind: command 8 must set all three components
        const before = ctx.alloc(v.vs_battle_actor);
        table[slot - 1] = before;
        status[slot - 1] &= ~@as(c_uint, 0x1000000);
        before.allegiance = 4;
        before.unk8 = 0x80;
        before.unk9 = 19;
        before.unkA = ctx.edge(u8, &.{ 16, 17 });
        before.unkB = ctx.int(u8);
        before.unkC = ctx.pick(i32, &.{ 0x1000, -0x1000, 0x7FFF, 0x12345678 });
        before.unk10.s32 = ctx.pick(i32, &.{ 0x2000, -0x1000, 0x7FFFFFFF });
        before.unk14 = ctx.pick(i32, &.{ 0x3000, -0x2000, 0x7FFFFFFF });
        psx.put(&before.unk44, null);
    }
    if (command == 24) {
        const shorts: *[2]u16 = @ptrCast(&actor.unkC);
        shorts[0] = ctx.range(u16, 0, 5);
        shorts[1] = ctx.range(u16, 0, 3);
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (scenario >= 9) return scenario;
    return scenario;
}
