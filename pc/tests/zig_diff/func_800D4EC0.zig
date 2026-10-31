//! zig_diff spec: func_800D4EC0 -> func_800D4EC0.func_800D4EC0 (5BF94.c).
//! Inputs: a D_800F53B8_t in the arena whose unkD1C.unk30 guest pointer is
//! null, a block in the arena, a non-arena address with the sign bit set
//! (kept as a pointer, not sign-extended), or a random 32-bit value. The free
//! is a recording stub: it writes nothing the C reads back, and the C only
//! reads unk30 before the call, so the stub needs no state of its own.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4EC0));

pub const classes = [_][:0]const u8{ "null", "arena-block", "high-bit-address", "other-address" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F53B8_t);
    switch (ctx.range(u8, 0, 3)) {
        0 => psx.put(&obj.unkD1C.unk30, @as(?*v.D_800F53B8_t4, null)),
        1 => psx.put(&obj.unkD1C.unk30, ctx.alloc(v.D_800F53B8_t4)),
        2 => {
            const a: usize = 0x8000_0000 | @as(usize, ctx.int(u32) & 0xFFFF_FFFC);
            const p: ?*v.D_800F53B8_t4 = @ptrFromInt(a);
            psx.put(&obj.unkD1C.unk30, p);
        },
        else => {
            const a: usize = @as(usize, ctx.int(u32) & 0xFFFF_FFFC);
            const p: ?*v.D_800F53B8_t4 = if (a == 0) null else @ptrFromInt(a);
            psx.put(&obj.unkD1C.unk30, p);
        },
    }
    return .{obj};
}

pub fn classify(args: Args) usize {
    const raw: u32 = psx.rawOf(args[0].?.unkD1C.unk30);
    if (raw == 0) return 0;
    const lo: u32 = psx.addr(&zd.zdiff_arena[0]);
    if (raw >= lo and raw - lo < zd.arena_size) return 1;
    if (raw & 0x8000_0000 != 0) return 2;
    return 3;
}
