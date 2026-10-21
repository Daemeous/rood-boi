//! zig_diff spec: func_800E5698 -> action_range_squared.actionRangeSquared (7CD68.c).
//! Inputs: an action index 0-255 whose rangeX is 0xFF (the range comes from
//! arg0's unk5C object, which prepare allocates in the arena), 0xFE (the
//! neighbouring sentinel, so a wrong sentinel must take the other branch
//! with a real object), or another value (the range is the action's own
//! rangeX bytes). arg0 is null only for the other values, where the C never
//! reads it, so the null path stays crash-free in both runs. The callee
//! func_800E5568 is pure as far as the C can tell: it reads its range
//! argument and writes nothing the C reads back, so its stub only returns
//! values. The stub returns 0, 0x20 (the equal case of `v > 0x20`), 0x21,
//! 0x1F, -1 (an unsigned value above 0x20 after the conversion), a value
//! whose square wraps u_int, and random 32-bit values.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").actionRangeSquared));

pub const classes = [_][:0]const u8{ "action-range", "object-range" };

pub const stubs = struct {
    pub fn func_800E5568(ctx: *zd.Ctx, range: anytype) c_int {
        _ = range;
        return switch (ctx.value(u8) % 8) {
            0 => 0,
            1 => 0x20,
            2 => 0x21,
            3 => 0x1F,
            4 => -1,
            5 => 0x10001,
            6 => -0x7FFFFFFF,
            else => ctx.value(c_int),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actionId: c_int = ctx.range(c_int, 0, 255);
    const actions = v.g.actions();
    const action = &actions[@intCast(actionId)];

    action.rangeX = if (ctx.chance(2)) 0xFF else ctx.pick(u8, &.{ 0, 0xFE, 0x7F, 0x80 });

    // arg0 is dereferenced only when rangeX is 0xFF. For the sentinel
    // neighbour 0xFE a valid object is always given, so a wrong sentinel
    // takes the object path and differs from the C without faulting. Only
    // for other values may arg0 be null, where the C never reads it.
    var arg0: ?*v.func_800E5698_t = null;
    if (action.rangeX == 0xFF or action.rangeX == 0xFE or !ctx.chance(3)) {
        const self = ctx.alloc(v.func_800E5698_t);
        const sub = ctx.alloc(v.func_800E5698_t2);
        psx.put(&self.unk5C, sub);
        arg0 = self;
    }
    return .{ arg0, actionId };
}

pub fn classify(args: Args) usize {
    const action = &v.g.actions()[@intCast(args[1])];
    return if (action.rangeX == 0xFF) 1 else 0;
}
