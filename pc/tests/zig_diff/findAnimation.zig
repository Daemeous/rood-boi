//! zig_diff spec: func_800AD494 -> animation_lookup.findAnimation (40564.c).
//! Inputs: an arena actor whose five table fields (unk5D0, unk5D4, unk5D8,
//! unk5DC[4]) and D_800F4768 are arena tables or NULL; each table's unk4 is
//! a 256-entry index (0xFF: an empty entry) with room before it, as a
//! negative animation reads there; the counts unk5B1/unk5B4-unk5B6, unkF,
//! unk6E6 and D_800F49E4 at their edges; D_800E8F2C 0-3. Animations: 0,
//! 0xFF, 0xFE, 0xDC-0x1DB, 0xCA-0xDB, 100-0xC9, 1-99 and -16..-1; `frames`
//! is an arena slot. The result is a guest address (psx_addr).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").findAnimation));

pub const classes = [_][:0]const u8{ "anim-0", "ff-index", "ff-slot", "fe", "dc", "ca", "100", "low", "negative",
    "no-slot", "past-count", "no-table", "empty-entry" };

const margin = 32;

fn table(ctx: *zd.Ctx) ?*v.func_800AD494_t {
    if (ctx.chance(10)) return null;
    const t: *v.func_800AD494_t = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
    const index = ctx.allocBytes(margin + 256, 16);
    for (0..margin + 256) |i| index[i] = if (ctx.chance(6)) 0xFF else ctx.int(u8);
    psx.put(&t.unk4, index + margin);
    return t;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    psx.put(&actor.unk5D0, table(ctx));
    psx.put(&actor.unk5D4, table(ctx));
    psx.put(&actor.unk5D8, table(ctx));
    for (&actor.unk5DC) |*slot| psx.put(slot, table(ctx));
    v.g.D_800F4768().* = table(ctx);
    v.g.D_800E8F2C().* = ctx.range(u8, 0, 3);
    v.g.D_800F49E4().* = ctx.pick(u8, &.{ 0, 1, 31, 254, 255 });
    actor.unk5B1 = ctx.pick(u8, &.{ 0, 1, 2, 10, 85, 86, 255 });
    actor.unk5B4 = ctx.pick(u8, &.{ 0, 1, 50, 99, 255 });
    actor.unk5B5 = ctx.pick(u8, &.{ 0, 1, 50, 101, 255 });
    actor.unk5B6 = ctx.pick(u8, &.{ 0, 1, 32, 34, 255 });
    actor.unk0.unkF = if (ctx.chance(2)) 0 else ctx.int(u8);
    actor.unk6E6 = if (ctx.chance(3)) 2 else ctx.int(u16);
    const animation: c_int = switch (ctx.range(u8, 0, 9)) {
        0 => 0,
        1 => 0xFF,
        2 => 0xFE,
        3 => ctx.range(c_int, 0xDC, 0x1DB),
        4 => ctx.range(c_int, 0xCA, 0xDB),
        5 => ctx.range(c_int, 100, 0xC9),
        6, 7 => ctx.range(c_int, 1, 99),
        else => ctx.range(c_int, -16, -1),
    };
    const frames: *[*c]u16 = @ptrCast(@alignCast(ctx.allocBytes(8, 8)));
    return .{ actor, animation, frames };
}

fn tableOf(p: psx.Ptr(anyopaque)) ?*v.func_800AD494_t {
    return @ptrCast(@alignCast(psx.get(p)));
}

pub fn classify(args: Args) usize {
    const actor: *v.D_800F4538_t = @ptrCast(@alignCast(args[0].?));
    const a = args[1];
    var path: usize = undefined;
    var p: ?*v.func_800AD494_t = undefined;
    var idx: c_int = undefined;
    if (a == 0) {
        path = 0;
        p = tableOf(actor.unk5D0);
        idx = 0;
    } else if (a == 0xFF or a == 0xFE) {
        p = tableOf(actor.unk5DC[3]);
        if (a == 0xFF and (actor.unk0.unkF == 0 or actor.unk6E6 == 2)) {
            path = 1;
            idx = v.g.D_800F49E4().*;
        } else {
            if (actor.unk5B1 == 0) return 9;
            path = if (a == 0xFF) 2 else 3;
            idx = (@as(c_int, actor.unk5B1) - 1) * 3 + (if (a == 0xFF) @as(c_int, v.g.D_800F49E4().*) else 31);
        }
        if (idx >= actor.unk5B6) return 10;
    } else if (a >= 0xDC) {
        path = 4;
        p = tableOf(actor.unk5DC[v.g.D_800E8F2C().*]);
        idx = a - 0xDC;
    } else if (a >= 0xCA) {
        path = 5;
        p = @ptrCast(@alignCast(v.g.D_800F4768().*));
        idx = a - 0xCA;
    } else if (a >= 100) {
        path = 6;
        p = tableOf(actor.unk5D8);
        idx = a - 100;
        if (idx >= actor.unk5B5) return 10;
    } else {
        path = if (a < 0) 8 else 7;
        p = tableOf(actor.unk5D4);
        idx = a;
        if (idx >= actor.unk5B4) return 10;
    }
    const t = p orelse return 11;
    const entry = psx.at(psx.get(t.unk4).?, idx).*;
    return if (entry == 0xFF) 12 else path;
}
