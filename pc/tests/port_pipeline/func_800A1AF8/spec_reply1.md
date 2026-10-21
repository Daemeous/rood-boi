```zig
//! zig_diff spec: func_800A1AF8 -> func_800A1AF8.func_800A1AF8 (38C1C.c).
//! Inputs: any actor id, bone and endpoint, a result buffer in the arena;
//! func_800A1C10 returns a negative value, zero or a positive one (its
//! edges: -1, 0, 1, the extremes), so both returns are taken.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1AF8));

pub const classes = [_][:0]const u8{ "callee-negative", "callee-zero", "callee-positive" };

var callee_result: c_int = 0;

pub const stubs = struct {
    pub fn func_800A1C10(ctx: *zd.Ctx, _: c_int, _: c_int, _: [*c]c_ushort, _: c_int) c_int {
        _ = ctx;
        return callee_result;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    callee_result = ctx.pick(c_int, &.{ -1, 0, 0, 1, std.math.minInt(c_int), std.math.maxInt(c_int), -4 });
    const result: [*c]c_ushort = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    return .{ ctx.int(c_int), ctx.int(c_int), result, ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    return if (callee_result < 0) 0 else if (callee_result == 0) 1 else 2;
}
```

```toml
[function.func_800A1AF8]
spec = "func_800A1AF8.zig"
iterations = 2000

[[function.func_800A1AF8.mutation]]
name = "the negative result dropped"
target = "c"
from = "return var_v0;"
to = "return 0;"

[[function.func_800A1AF8.mutation]]
name = "an unsigned test of the result"
target = "c"
from = "if (var_v0 < 0) {"
to = "if ((unsigned)var_v0 < 0) {"

[[function.func_800A1AF8.mutation]]
name = "arguments swapped"
target = "c"
from = "func_800A1C10(arg0, arg1, arg2, arg3)"
to = "func_800A1C10(arg1, arg0, arg2, arg3)"
```
