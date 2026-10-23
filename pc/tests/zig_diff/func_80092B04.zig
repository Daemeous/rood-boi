//! zig_diff spec: func_80092B04 -> func_80092B04.func_80092B04 (2842C.c).
//! Inputs: an array of 8 actors in the arena, arg1 from -2 to 8 (the loop is
//! skipped for zero or less, and arg1 == 8 reads every slot), each actor's
//! unk4 either null or a short script in the arena. Scripts are chains of
//! entries that only move forward, ending in one of three terminals: unk0 == 0,
//! unk2 == 13 (the bound, both sides equal), or a negative unk0 with unk2 past
//! the table. vs_main_stateFlags.unkA8 is 0, 1 or 0xFF, which selects the
//! auto-repeat branch. The table D_800E85E8 is overwritten with one stub
//! handler that draws its return and may rewrite the entry it is handed
//! (unk0 to 0, unk2 to a valid index 0..12, so the table is never read out of
//! range). Its return is 0 or nonzero with both signs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092B04));

pub const classes = [_][:0]const u8{ "no-actors", "auto-state", "first-idle", "chain" };

const N = 8;

var active: ?*zd.Ctx = null;

/// The table handler: a callee that may end the script entry it was given
/// (unk0 = 0) or move it to another valid op (unk2 in 0..12), and returns 0
/// or a nonzero value of either sign.
fn step(a: ?*anyopaque, t_opt: ?*anyopaque) callconv(.c) c_int {
    _ = a;
    const ctx = active.?;
    ctx.calls += 1;
    ctx.draws = 0;
    const t: *v.func_80092B04_t2 = @ptrCast(@alignCast(t_opt.?));
    if (ctx.value(u2) == 0) t.unk0 = 0;
    if (ctx.value(u2) == 0) t.unk2 = @intCast(ctx.value(u8) % 13);
    if (ctx.value(u1) == 0) return 0;
    return ctx.value(c_int) | 1;
}

fn buildScript(ctx: *zd.Ctx) *anyopaque {
    const base = ctx.allocBytes(64, 8);
    const k = ctx.range(u8, 1, 4);
    var off: usize = 0;
    var j: u8 = 0;
    while (j < k) : (j += 1) {
        const e: *v.func_80092B04_t2 = @ptrCast(@alignCast(base + off));
        if (j + 1 < k) {
            const s = ctx.edge(i16, &.{ 4, 8 });
            e.unk0 = s;
            e.unk2 = ctx.edge(i16, &.{ 0, 1, 5, 11, 12 });
            off += @intCast(s);
        } else switch (ctx.range(u8, 0, 2)) {
            // unk0 == 0 ends the chain
            0 => {
                e.unk0 = 0;
                e.unk2 = ctx.edge(i16, &.{ 0, 1, 5, 11, 12 });
            },
            // the bound: unk2 == 13 is not < 0xD
            1 => {
                e.unk0 = 4;
                e.unk2 = ctx.edge(i16, &.{ 13, 0x7FFF });
            },
            // a negative unk0 (sign bit set) with the bound past
            else => {
                e.unk0 = ctx.edge(i16, &.{ -4, -1 });
                e.unk2 = ctx.edge(i16, &.{ 13, 0x7FFF });
            },
        }
    }
    return @ptrCast(base);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    active = ctx;

    const flags = v.g.vs_main_stateFlags();
    flags.unkA8 = ctx.edge(u8, &.{ 0, 1, 0xFF });

    const tbl = v.g.D_800E85E8();
    for (tbl) |*slot| slot.* = @ptrCast(&step);

    const base = ctx.allocBytes(N * @sizeOf(v.func_80092B04_t), 16);
    const actors: [*]v.func_80092B04_t = @ptrCast(@alignCast(base));
    for (0..N) |i| {
        const a = &actors[i];
        psx.put(&a.unk4, if (ctx.chance(4)) null else buildScript(ctx));
    }

    const arg1 = ctx.edge(c_int, &.{ -2, -1, 0, 1, 2, 3, 5, 7, 8 });
    const arg2: ?*v.D_800F1DD8_t = if (ctx.chance(2)) null else ctx.alloc(v.D_800F1DD8_t);
    return .{ &actors[0], arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[1] <= 0) return 0;
    if (v.g.vs_main_stateFlags().unkA8 != 0) return 1;
    const first = args[0].?;
    if (psx.get(first.unk4) == null) return 2;
    return 3;
}
