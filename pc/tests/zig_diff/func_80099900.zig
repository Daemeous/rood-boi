//! zig_diff spec: func_80099900 -> func_80099900.func_80099900 (30DB0.c).
//! Inputs: an arg object with any index (own slot, -1, -128, 127, 16) and an
//! actorId that is 0-7 most of the time (the only values a 3-bit actorId can
//! equal), sometimes any u8 so the 3-bit comparison is exercised. The 16 slots
//! of D_800F45E0 are arena objects or NULL. Up to two slots are made to match
//! (the first match is returned, so the second only shows which one wins), the
//! rest never match, and the arg's index is the matching slot about a third of
//! the time, so the `i != index` skip is taken.
//! Classes: no match, the only match skipped by index, one match, several.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80099900));

pub const classes = [_][:0]const u8{ "no-match", "index-skipped", "one-match", "several-matches" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.vs_battle_objectData);
    const table = v.g.D_800F45E0();

    arg.actorId = if (ctx.chance(4)) ctx.int(u8) else ctx.range(u8, 0, 7);
    const own = arg.actorId < 8;
    const want: u3 = if (own) @truncate(arg.actorId) else 0;
    const hit: ?usize = if (ctx.chance(3)) null else ctx.range(usize, 0, 15);
    const hit2: ?usize = if (ctx.chance(4)) ctx.range(usize, 0, 15) else null;

    arg.index = if (hit != null and ctx.chance(3))
        @as(i8, @intCast(hit.?))
    else
        ctx.pick(i8, &.{ 0, 5, 15, 16, -1, -128, 127 });
    arg.dataAddr = ctx.int(u32);

    for (table, 0..) |*slot, i| {
        if (ctx.chance(5)) {
            slot.* = null;
            continue;
        }
        const obj = ctx.alloc(v.D_800F45E0_t);
        slot.* = obj;
        const is_hit = (hit != null and hit.? == i) or (hit2 != null and hit2.? == i);
        var value: u3 = ctx.range(u3, 0, 7);
        if (is_hit and own) {
            value = want;
        } else if (own and value == want) {
            value +%= 1;
        }
        psx.setBits(&obj.unk6C[8], "actorId", value);
    }
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0].?;
    const table = v.g.D_800F45E0();
    var hits: usize = 0;
    var skipped = false;
    for (table, 0..) |slot, i| {
        const o = slot orelse continue;
        if (@as(u8, psx.getBits(&o.unk6C[8], "actorId")) != arg.actorId) continue;
        if (@as(i32, @intCast(i)) == @as(i32, arg.index)) {
            skipped = true;
        } else {
            hits += 1;
        }
    }
    if (hits == 0) return if (skipped) 1 else 0;
    return if (hits == 1) 2 else 3;
}
