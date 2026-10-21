```zig
//! zig_diff spec: func_800E5698 -> func_800E5698.func_800E5698 (7CD68.c).
//! Inputs: an action index 0-255 with its rangeX either 0xFF (the range comes
//! from arg0's pointer, which is an arena object, or null so the C reads
//! PS1 address 0) or any other byte (the range is the action's own rangeX
//! bytes). func_800E5568 is a stub that returns 0x20 (the equal side of
//! `v > 0x20`), 0x21, 0, 0x80000000 (negative as signed, large as unsigned),
//! 0xFFFFFFFF and random 32-bit values, so both sides of the clamp and the
//! wrapping square are reached.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E5698));

pub const classes = [_][:0]const u8{ "literal-range", "ptr-range-arena", "ptr-range-lowram" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actionId: c_int = ctx.range(u8, 0, 255);
    const action = &v.g.actions()[@intCast(actionId)];
    const arg0 = ctx.alloc(v.func_800E5698_t);
    if (ctx.chance(2)) {
        action.rangeX = 0xFF;
        const range = ctx.alloc(v.func_800E5698_t2);
        psx.put(&arg0.unk5C, if (ctx.chance(4)) null else range);
    } else {
        action.rangeX = ctx.range(u8, 0, 254);
    }
    return .{ arg0, actionId };
}

pub fn classify(args: Args) usize {
    const action = &v.g.actions()[@intCast(args[1])];
    if (action.rangeX != 0xFF) return 0;
    return if (psx.rawOf(args[0].?.unk5C) == 0) 2 else 1;
}

pub const stubs = struct {
    /// Returns the value the C tests against 0x20 and the wrap-around edges.
    /// The callee does not change the range the C reads back, so nothing is written.
    pub fn func_800E5568(ctx: *zd.Ctx, range: anytype) c_int {
        _ = range;
        const r: u32 = switch (ctx.value(u3)) {
            0, 1 => 0x20,
            2 => 0x21,
            3 => 0,
            4 => 0x8000_0000,
            5 => 0xFFFF_FFFF,
            else => ctx.value(u32),
        };
        return @bitCast(r);
    }
};
```

```toml
[function.func_800E5698]
spec = "func_800E5698.zig"
iterations = 30000

[[function.func_800E5698.mutation]]
name = "F1: clamp at >= 0x20 (the value 0x20 becomes 0 rather than 0)"
from = "if (v > 0x20) {"
to = "if (v >= 0x20) {"

[[function.func_800E5698.mutation]]
name = "F2: wrong subtraction constant"
from = "v -= 0x20;"
to = "v -= 0x21;"

[[function.func_800E5698.mutation]]
name = "F3: signed comparison of the unsigned result"
from = "if (v > 0x20) {"
to = "if ((int)v > 0x20) {"

[[function.func_800E5698.mutation]]
name = "F4: the 0xFF test reads rangeY"
from = "if (action->rangeX == 0xFF) {"
to = "if (action->rangeY == 0xFF) {"

[[function.func_800E5698.mutation]]
name = "F5: literal range points at rangeY"
from = "range = (func_800E5568_t*)&action->rangeX;"
to = "range = (func_800E5568_t*)&action->rangeY;"

[[function.func_800E5698.mutation]]
name = "F6: wrong constant added to the square"
from = "return v * v;"
to = "return v * v + 1;"
```