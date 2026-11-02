//! zig_diff spec: func_800A9530 -> func_800A9530.func_800A9530 (40564.c).
//! Inputs: a fresh actor (unk63C 0..300 or any; position near cell
//! boundaries or any; the flag word at 0x5AC with its top nibble and bit
//! 0x1000 set or clear) and a random offset; the facing D_800F49F9 (0..7,
//! or any byte). unk0.unkC_0 is 0 (the cell branch) or a platform id 1..15:
//! func_8008D2C0's stub lists 0..4 platforms into the table it is handed (the
//! inlined copy's static `platforms`, globalised: views
//! func_800A4A24__platforms), one of them id - 2 unless the class wants
//! none (then the C reads PS1 address 0, low RAM). rsin's stub returns any
//! int; func_800A70DC's sets or clears the flag word's 0x1000 (the probe
//! blocked or not) and moves the actor.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9530));

pub const classes = [_][:0]const u8{ "cell-x", "cell-z", "platform-x", "platform-z", "no-platform" };

var count: c_int = 0;
var hit: c_int = -1;
var want: i16 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const offset = ctx.alloc(v.SVECTOR);
    actor.unk63C = if (ctx.chance(3)) ctx.int(u16) else ctx.range(u16, 0, 300);
    actor.unk0.position.vx = if (ctx.chance(2)) ctx.pick(i16, &.{ 0, 63, 64, 127, 128, -1, -128 }) else ctx.int(i16);
    actor.unk0.position.vz = if (ctx.chance(2)) ctx.pick(i16, &.{ 0, 63, 64, 127, 128, -1, -128 }) else ctx.int(i16);
    offset.vx = ctx.pick(i16, &.{ 0, 32, 33, -32, -33, 96, -96 });
    offset.vz = ctx.pick(i16, &.{ 0, 32, 33, -32, -33, 96, -96 });
    const flags = psx.byteAt(u32, actor, 0x5AC);
    flags.* = ctx.int(u32);
    if (ctx.chance(2)) flags.* &= 0x0FFF_FFFF;
    if (ctx.chance(8)) flags.* = ctx.edge(u32, &.{ 0x0FFF_FFFF, 0x1000_0000, 0x0FFF_EFFF });
    v.g.D_800F49F9().* = if (ctx.chance(4)) ctx.int(u8) else ctx.range(u8, 0, 7);

    const id: u4 = if (ctx.chance(3)) 0 else ctx.range(u4, 1, 15);
    psx.setBits(&actor.unk0, "unkC_0", id);
    count = ctx.range(c_int, 0, 4);
    want = @as(i16, id) - 2;
    hit = if (count == 0 or ctx.chance(4)) -1 else ctx.range(c_int, 0, count - 1);
    for (v.g.func_800A4A24__platforms()) |*e| e.id = want +% 100;
    return .{ actor, offset };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const odd = (v.g.D_800F49F9().* >> 1) & 1 != 0;
    if (psx.getBits(&actor.unk0, "unkC_0") == 0) return if (odd) 0 else 1;
    if (hit < 0) return 4;
    return if (odd) 2 else 3;
}

pub const stubs = struct {
    pub fn func_8008D2C0(ctx: *zd.Ctx, table: anytype) c_int {
        const entries: [*]v.func_8008D2C0_t = @ptrCast(table);
        var i: c_int = 0;
        while (i < count) : (i += 1) {
            const e = &entries[@intCast(i)];
            e.x = ctx.value(i16);
            e.y = ctx.value(i16);
            e.z = ctx.value(i16);
            e.id = if (i == hit or (hit >= 0 and i > hit and ctx.value(u2) == 0))
                want
            else
                want +% @as(i16, ctx.value(u3)) +% 1;
        }
        return count;
    }

    pub fn func_800A70DC(ctx: *zd.Ctx, actor: anytype, distance: anytype) void {
        _ = distance;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
        const flags = psx.byteAt(u32, a, 0x5AC);
        if (ctx.value(u1) == 1) flags.* |= 0x1000 else flags.* &= ~@as(u32, 0x1000);
        if (ctx.value(u2) == 0) a.unk0.position.vx +%= ctx.value(i16);
    }
};
