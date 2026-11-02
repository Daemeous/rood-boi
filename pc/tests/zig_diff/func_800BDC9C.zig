//! zig_diff spec: func_800BDC9C -> func_800BDC9C.func_800BDC9C (4A0A8.c).
//! Inputs: a D_800F4BA4_t in the arena, installed in the global pointer, and
//! an 8-byte script. Most scripts have an opcode of 234, 235, 236 or 239 (the
//! four cases the switch handles); about one in four has an opcode outside the
//! switch, so the fall-through past the switch is taken. Operands are drawn
//! from 0, 1, 2, 127, 128, 255 and random bytes, so the `? :` zero tests see
//! zero and nonzero values with and without the sign bit. The clip cases cover
//! the third byte zero and nonzero. For 239 the two entry slots are free or
//! taken in every combination, so the loop sometimes runs out of slots (the
//! `i < 2` exit). The C makes no external calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BDC9C));

pub const classes = [_][:0]const u8{
    "234 flag0",
    "234 flag1",
    "235 flag0",
    "235 flag1",
    "236 flag0",
    "236 flag1",
    "239 operand2 zero",
    "239 slot0 free",
    "239 slot1 free",
    "239 no free slot",
    "opcode outside the switch",
};

/// The two entry slots: entry[0] is at &unk1A4.unk2, entry[1] is 12 bytes on.
fn entries(obj: *v.D_800F4BA4_t) [2]*v.func_800BDF6C_t {
    const base: [*]u8 = @ptrCast(&obj.unk1A4.unk2);
    const e0: *v.func_800BDF6C_t = @ptrCast(@alignCast(base));
    const e1: *v.func_800BDF6C_t = @ptrCast(@alignCast(base + 12));
    return .{ e0, e1 };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = obj;

    const s = ctx.allocBytes(8, 16);
    const operand = [_]u8{ 0, 1, 2, 127, 128, 255 };
    for (0..8) |i| s[i] = ctx.pick(u8, &operand);
    s[0] = switch (ctx.range(u8, 0, 4)) {
        0 => 234,
        1 => 235,
        2 => 236,
        3 => 239,
        // Not a case label: the C leaves the switch and falls off the end.
        else => ctx.edge(u8, &.{ 0, 1, 233, 237, 238, 240, 255 }),
    };
    // The flag byte (script[3]) of the clip cases: zero about half the time.
    if (ctx.chance(2)) s[3] = 0;

    if (s[0] == 239) {
        // script[2] is the operand the C tests against zero.
        if (ctx.chance(4)) {
            s[2] = 0;
        } else if (s[2] == 0) {
            s[2] = 1;
        }
        // Slot states: 0 slot0 free, 1 slot1 free, 2 none free, 3 both free.
        const mode = ctx.range(u8, 0, 3);
        const e = entries(obj);
        e[0].unk9 = if (mode == 0 or mode == 3) 0 else ctx.range(u8, 1, 255);
        e[1].unk9 = if (mode == 1 or mode == 3) 0 else ctx.range(u8, 1, 255);
    }
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const s = args[0];
    return switch (s[0]) {
        234 => if (s[3] == 0) 0 else 1,
        235 => if (s[3] == 0) 2 else 3,
        236 => if (s[3] == 0) 4 else 5,
        239 => blk: {
            if (s[2] == 0) break :blk 6;
            const obj = v.g.D_800F4BA4().*.?;
            const e = entries(obj);
            if (e[0].unk9 == 0) break :blk 7;
            if (e[1].unk9 == 0) break :blk 8;
            break :blk 9;
        },
        else => 10,
    };
}
