```zig
//! zig_diff spec: func_8006B214 -> func_8006B214.func_8006B214 (146C.c).
//! Inputs: the character's stats in the arena with status bit 0x100000 set
//! or clear (and its neighbours), unk957 any byte.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8006B214));

pub const classes = [_][:0]const u8{ "status-set", "status-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const character = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    psx.put(&character.stats, stats);
    v.g.vs_battle_characterState().* = character;
    stats.statuses = switch (ctx.range(u8, 0, 3)) {
        0 => 0x100000,
        1 => ctx.int(i32) | 0x100000,
        2 => ctx.int(i32) & ~@as(i32, 0x100000),
        else => ctx.pick(i32, &.{ 0, 0x80000, 0x200000, -1 }),
    };
    return .{};
}

pub fn classify(_: Args) usize {
    const stats = psx.get(v.g.vs_battle_characterState().*.?.stats).?;
    return if (stats.statuses & 0x100000 != 0) 0 else 1;
}
```

```toml
[function.func_8006B214]
spec = "func_8006B214.zig"
iterations = 20000

[[function.func_8006B214.mutation]]
name = "the status bit 0x80000"
target = "c"
from = "temp_s0->statuses & 0x100000"
to = "temp_s0->statuses & 0x80000"

[[function.func_8006B214.mutation]]
name = "func_80086754 dropped"
target = "c"
from = "func_80086754(0x100000, temp_s0);"
to = ";"

[[function.func_8006B214.mutation]]
name = "the first call's last argument 0"
target = "c"
from = "0x100000, 1);"
to = "0x100000, 0);"
```
