//! zig_diff spec: func_800A47C4 -> func_800A47C4.func_800A47C4 (3A1A0.c).
//! Inputs: the 16-entry pointer table D_800F45E0, each entry null or an arena
//! object. One planned hit index (none, first, middle, last, or random) gets an
//! object with unk8_0 clear, unk9_0 clear and a nonzero unk1A (positive and
//! negative values). Every other non-null entry is blocked by exactly one of
//! unk8_0 set, unk9_0 nonzero or unk1A zero. The other bitfield and field bytes
//! keep the random fill, so a wrong bit or field read shows up.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A47C4));

pub const classes = [_][:0]const u8{ "no-hit", "hit-first", "hit-middle", "hit-last" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F45E0();
    const hit_at: usize = switch (ctx.range(u8, 0, 4)) {
        0 => 16, // no hit planned
        1 => 0,
        2 => ctx.range(usize, 1, 14),
        3 => 15,
        else => ctx.range(usize, 0, 15),
    };
    for (table, 0..) |*slot, i| {
        if (i != hit_at and ctx.chance(4)) {
            slot.* = null;
            continue;
        }
        const obj = ctx.alloc(v.D_800F45E0_t);
        if (i == hit_at) {
            psx.setBits(obj, "unk8_0", 0);
            psx.setBits(obj, "unk9_0", 0);
            var h = ctx.pick(i16, &.{ 1, -1, 32767, -32768, 0x100 });
            if (h == 0) h = 1;
            obj.unk1A = h;
        } else {
            switch (ctx.range(u8, 0, 2)) {
                0 => {
                    psx.setBits(obj, "unk8_0", 1);
                },
                1 => {
                    psx.setBits(obj, "unk9_0", ctx.range(u4, 1, 15));
                },
                else => {
                    obj.unk1A = 0;
                },
            }
        }
        slot.* = obj;
    }
    return .{};
}

fn isHit(p: ?*v.D_800F45E0_t) bool {
    const o = p orelse return false;
    return psx.getBits(o, "unk8_0") == 0 and psx.getBits(o, "unk9_0") == 0 and o.unk1A != 0;
}

pub fn classify(args: Args) usize {
    _ = args;
    const table = v.g.D_800F45E0();
    for (table, 0..) |p, i| {
        if (isHit(p)) {
            if (i == 0) return 1;
            if (i == 15) return 3;
            return 2;
        }
    }
    return 0;
}
