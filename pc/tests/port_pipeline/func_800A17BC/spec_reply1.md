```zig
//! zig_diff spec: func_800A17BC -> func_800A17BC.func_800A17BC (38C1C.c).
//! Inputs: an actor slot 0..9 whose D_800F4538 entry is an actor or empty
//! (then its D_800F45E0 entry an object or empty), its weapon model slot
//! filled or empty; the bone 0xFF, a weapon bone 0x40..0x4F or a body bone;
//! func_800A1DE8 (stubbed with the views types) returns the matrix it is
//! given or NULL (the C then reads PS1 address 0, low RAM).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A17BC));

pub const classes = [_][:0]const u8{ "empty", "fallback", "bone-ff", "weapon", "no-weapon", "bone" };

var matrix_null = false;

pub const stubs = struct {
    pub fn func_800A1DE8(_: *zd.Ctx, _: c_int, _: c_int, m: ?*v.MATRIX) ?*v.MATRIX {
        return if (matrix_null) null else m;
    }
};

fn model(ctx: *zd.Ctx) *v.D_800F4538_unk68 {
    return @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.D_800F4538_unk68) + 256 * @sizeOf(v._armature_t), 16)));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = ctx.range(usize, 0, 9);
    matrix_null = ctx.chance(8);
    const actor = ctx.alloc(v.D_800F4538_t);
    psx.put(&actor.unk0.unk68, model(ctx));
    const obj = ctx.alloc(v.D_800F4538_t);
    psx.put(&obj.unk0.unk68, model(ctx));
    const wep = ctx.alloc(v.vs_battle_wepModels_t);
    psx.put(&wep.offsets, model(ctx));
    const kind = ctx.range(u8, 0, 3);
    v.g.D_800F4538()[slot] = if (kind <= 1) null else actor;
    v.g.D_800F45E0()[slot] = if (kind == 0) null else @ptrCast(@alignCast(obj));
    v.g.vs_battle_wepModels()[slot * 2] = if (ctx.chance(4)) null else wep;
    v.g.vs_battle_wepModels()[slot * 2 + 1] = null;
    const bone: c_int = switch (ctx.range(u8, 0, 2)) {
        0 => 0xFF,
        1 => ctx.range(c_int, 0x40, 0x4F),
        else => ctx.range(c_int, 0, 0x3F),
    };
    const out: *v.MATRIX = ctx.alloc(v.MATRIX);
    const result: *c_int = @ptrCast(@alignCast(ctx.allocBytes(4, 16)));
    return .{ @intCast(slot), bone, out, result };
}

pub fn classify(args: Args) usize {
    const slot: usize = @intCast(args[0]);
    if (v.g.D_800F4538()[slot] == null) return if (v.g.D_800F45E0()[slot] == null) 0 else 1;
    if (args[1] == 0xFF) return 2;
    if (args[1] & 0xF0 == 0x40) return if (v.g.vs_battle_wepModels()[slot * 2] != null) 3 else 4;
    return 5;
}
```

```toml
[function.func_800A17BC]
spec = "func_800A17BC.zig"
iterations = 4000

[[function.func_800A17BC.mutation]]
name = "bone 0xFF writes 1"
target = "c"
from = "*arg3 = 0;"
to = "*arg3 = 1;"

[[function.func_800A17BC.mutation]]
name = "the weapon bone base off by one"
target = "c"
from = "armatures[arg1 - 0x3F]"
to = "armatures[arg1 - 0x40]"

[[function.func_800A17BC.mutation]]
name = "the weapon model's neighbour slot"
target = "c"
from = "vs_battle_wepModels[arg0 * 2]"
to = "vs_battle_wepModels[arg0 * 2 + 1]"

[[function.func_800A17BC.mutation]]
name = "no low RAM fallback for a NULL matrix"
target = "c"
from = "*(MATRIX*)0x80000000"
to = "D_800F49B8"
```
