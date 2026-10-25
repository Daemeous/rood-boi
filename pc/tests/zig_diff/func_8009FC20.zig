//! zig_diff spec: func_8009FC20 -> func_8009FC20.func_8009FC20 (30DB0.c).
//! Inputs: a slot index 0..16 of D_800F4538, drawn mostly from the table's
//! ends and interior, with the slot either null or a fresh arena
//! D_800F4538_t whose unk6E5 is an edge byte (0, 0x7F, 0x80, 0xFF) or any
//! value. The out pointer is a fresh arena int with random contents, so a
//! store that is dropped or lands in the wrong place shows. The table is a
//! global, so it is set through v.g. No callees, so no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009FC20));

pub const classes = [_][:0]const u8{ "null-entry", "valid-entry", "valid-entry-slot-16" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // The C indexes the 17-entry table with no bounds check: stay inside it.
    const idx: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 1, 15, 16 })
    else
        ctx.range(c_int, 0, 16);

    const table = v.g.D_800F4538();
    const obj = ctx.alloc(v.D_800F4538_t);
    obj.unk6E5 = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    table[@intCast(idx)] = if (ctx.chance(3)) null else obj;

    const out = ctx.alloc(c_int);
    return .{ idx, out };
}

pub fn classify(args: Args) usize {
    const idx = args[0];
    if (v.g.D_800F4538()[@intCast(idx)] == null) return 0;
    return if (idx == 16) 2 else 1;
}
