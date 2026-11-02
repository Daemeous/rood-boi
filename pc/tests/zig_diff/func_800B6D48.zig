//! zig_diff spec: func_800B6D48 -> func_800B6D48.func_800B6D48 (4A0A8.c).
//! Inputs: a 16-byte script in the arena (script[1], script[6], script[7] and
//! script[8] are random bytes, with the width and height bytes at 0, 1, 255 so
//! the width/height arithmetic and the box size edges are hit). The callees
//! are stubs: func_800BB7C4 writes a random origin (any i16 vz, so the
//! `& 0xC00` mask sees the sign bit and every bit pattern). func_800B6B98 reads
//! the origin it is passed and writes a placement whose x is origin.vz, so the
//! origin's value reaches the placement and then the text box call. It succeeds
//! half the time (its result is any c_int, negative or positive, or zero) and
//! its distance is one of eight small values, so ties are common and the
//! fallback's `>` sees equal sides. vs_battle_getTextBox returns a box the
//! prepare allocated. The C's `texture` is never set (a known BUG); its value
//! is whatever the stack held.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B6D48));

pub const classes = [_][:0]const u8{ "zero-width", "max-width", "other-width" };

var box_obj: ?*v.vs_battle_textBox = null;

pub const stubs = struct {
    pub fn func_800BFE50(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return ctx.value(c_int);
    }

    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BB7C4(ctx: *zd.Ctx, arg0: anytype, p: anytype) c_int {
        _ = arg0;
        const o: *v.SVECTOR = @ptrCast(@alignCast(p));
        o.vx = ctx.value(i16);
        o.vy = ctx.value(i16);
        o.vz = ctx.value(i16);
        return ctx.value(c_int);
    }

    pub fn func_800B6B98(ctx: *zd.Ctx, origin: anytype, script: anytype, result: anytype, direction: anytype) c_int {
        _ = script;
        _ = direction;
        // The stub reads the origin the C computed: its vz (after the mask)
        // becomes the placement's x, which the text box call then records.
        const o: *const v.SVECTOR = @ptrCast(@alignCast(origin));
        const r: *v.textPlacement = @ptrCast(@alignCast(result));
        r.offset._bits_0 = @bitCast(ctx.value(u32));
        r.x = o.vz;
        r.y = ctx.value(i16);
        r.distance = @as(i32, ctx.value(i3));
        // Half the calls fail (so all four fail about 1 time in 16); a success
        // returns any c_int, negative or positive (and occasionally zero).
        if (ctx.value(u1) == 0) return 0;
        return ctx.value(c_int);
    }

    pub fn vs_battle_getTextBox(ctx: *zd.Ctx, arg0: anytype) ?*v.vs_battle_textBox {
        _ = ctx;
        _ = arg0;
        return box_obj;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const script: [*c]u8 = ctx.allocBytes(16, 16);
    script[1] = ctx.pick(u8, &.{ 0, 1, 31, 255 });
    script[6] = ctx.pick(u8, &.{ 0, 1, 255 });
    script[7] = ctx.pick(u8, &.{ 0, 1, 255 });
    box_obj = ctx.alloc(v.vs_battle_textBox);
    return .{ script, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const s = args[0];
    if (s[6] == 0) return 0;
    if (s[6] == 255) return 1;
    return 2;
}
