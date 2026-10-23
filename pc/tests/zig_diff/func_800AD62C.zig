//! zig_diff spec: func_800AD62C -> func_800AD62C.func_800AD62C (40564.c).
//! Inputs: an index 0-16 into D_800F4538 (the entry is set to an arena
//! D_800F4538_t, the others stay random); unk17E4.unk0 either 2 or not;
//! unk17E4.unk1 and unk2 from {0, 1, 0x80, 0xFF}, so each override is
//! nonzero with and without the sign bit; the six OR terms of the
//! state-check either all false (no SFX call) or exactly one true; arg1
//! always valid and arg2 null or valid. The SFX pan is a recording stub
//! returning any i32 (so the >> 16 and the & 0xFFFF see negatives), and
//! func_800A1280 may rewrite the unk6FC SVECTOR the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AD62C));

pub const classes = [_][:0]const u8{ "state2", "state2-null", "no-call", "call" };

fn orTerms(u: *v.D_800F4538_unk0, ctx: *zd.Ctx) void {
    // 0..5 makes exactly one OR term true (unk9_7 true means the term is its
    // negation being true, so it is cleared); 6..9 makes none true.
    const hot = ctx.range(u8, 0, 9);
    psx.setBits(u, "unk9_0", if (hot == 0) ctx.range(u4, 1, 15) else 0);
    psx.setBits(u, "unk9_4", if (hot == 1) 1 else 0);
    psx.setBits(u, "unk9_7", if (hot == 2) 0 else 1);
    psx.setBits(u, "unkA_0", if (hot == 3) ctx.range(u3, 1, 7) else 0);
    psx.setBits(u, "unkA_3", if (hot == 4) ctx.range(u2, 1, 3) else 0);
    psx.setBits(u, "unkA_5", if (hot == 5) 1 else 0);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const idx: c_int = @intCast(ctx.range(u8, 0, 16));
    v.g.D_800F4538()[@intCast(idx)] = obj;

    obj.unk17E4.unk0 = if (ctx.chance(3)) 2 else ctx.edge(u8, &.{ 0, 1, 3, 255 });
    obj.unk17E4.unk1 = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    obj.unk17E4.unk2 = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    orTerms(&obj.unk0, ctx);

    const p1: [*c]c_int = ctx.alloc(c_int);
    var p2: [*c]c_int = null;
    if (!ctx.chance(4)) p2 = ctx.alloc(c_int);
    return .{ idx, p1, p2 };
}

pub fn classify(args: Args) usize {
    const obj = v.g.D_800F4538()[@intCast(args[0])].?;
    if (obj.unk17E4.unk0 == 2) return if (args[2] == null) 1 else 0;
    const u = &obj.unk0;
    const hot = psx.getBits(u, "unk9_0") != 0 or
        psx.getBits(u, "unk9_4") != 0 or
        psx.getBits(u, "unk9_7") == 0 or
        psx.getBits(u, "unkA_0") != 0 or
        psx.getBits(u, "unkA_3") != 0 or
        psx.getBits(u, "unkA_5") != 0;
    return if (hot) 3 else 2;
}

pub const stubs = struct {
    // func_800A1280(int, int, SVECTOR *, int): the pointer is the third argument.
    pub fn func_800A1280(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) void {
        _ = .{ a, b, d };
        if (ctx.value(u1) == 1) {
            const sv: *v.SVECTOR = @ptrCast(@alignCast(c));
            sv.vx = ctx.value(i16);
            sv.vy = ctx.value(i16);
            sv.vz = ctx.value(i16);
        }
    }

    pub fn vs_main_computeSfxPan(ctx: *zd.Ctx, coords: anytype, z: anytype) c_int {
        _ = .{ coords, z };
        return ctx.value(c_int);
    }
};
