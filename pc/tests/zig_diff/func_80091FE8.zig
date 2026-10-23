//! zig_diff spec: func_80091FE8 -> func_80091FE8.func_80091FE8 (2842C.c).
//! Inputs: one func_80091FE8_t in the arena with every field the C reads set
//! by prepare. The tick and frame counters sit at their wrap and equality
//! edges (tick 255, frame 255, delay-1, frameCount-1), scrollStep is 0 or a
//! signed edge (never one that overflows `frame + scrollStep`), columns is
//! nonzero whenever the C divides by it, and rect fields take signed edges
//! (negative, odd, the i16 extremes, values that wrap under the -320/+256
//! steps). In the scroll path rect.h is set to equal the stepped frame in a
//! share of runs, so `frame >= rect.h` sees equal sides. MoveImage is a
//! recording stub: it copies the rectangle and position it is given into an
//! arena log (the C's stack RECTs are not compared, so the log carries them
//! into the comparison), and it draws no globals; the C reads nothing back
//! from it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091FE8));

pub const classes = [_][:0]const u8{ "idle", "step0-advance", "scroll-wrap-block", "scroll-no-wrap-block" };

/// Arena log of the MoveImage calls in the current run: 8 calls of
/// (rect.x, rect.y, rect.w, rect.h, x, y). Allocated by prepare each iteration.
var log: ?*[48]i32 = null;

pub const stubs = struct {
    pub fn MoveImage(ctx: *zd.Ctx, rect: anytype, x: anytype, y: anytype) c_int {
        const r: *const v.RECT = @ptrCast(@alignCast(rect));
        const base = (ctx.calls % 8) * 6;
        if (log) |l| {
            l[base + 0] = r.x;
            l[base + 1] = r.y;
            l[base + 2] = r.w;
            l[base + 3] = r.h;
            l[base + 4] = @intCast(x);
            l[base + 5] = @intCast(y);
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = ctx.alloc([48]i32);
    const obj = ctx.alloc(v.func_80091FE8_t);

    obj.scrollStep = if (ctx.chance(2)) 0 else ctx.edge(i32, &.{ 1, -1, 2, -2, 255, -255, 256, -256, 0x7FFF_FF00, -0x7FFF_FF00 });
    obj.columns = ctx.edge(u8, &.{ 0, 1, 2, 3, 7, 255 });
    if (obj.scrollStep == 0 and obj.columns == 0) obj.columns = 1;

    obj.delay = ctx.pick(u8, &.{ 0, 1, 2, 3, 254, 255 });
    obj.tick = if (ctx.chance(3)) obj.delay -% 1 else ctx.pick(u8, &.{ 0, 1, 2, 254, 255 });

    obj.frame = ctx.pick(u8, &.{ 0, 1, 2, 254, 255 });
    obj.frameCount = ctx.pick(u8, &.{ 0, 1, 2, 3, 254, 255 });
    if (obj.scrollStep == 0 and ctx.chance(3)) obj.frameCount = obj.frame +% 1;

    const r = &obj.rect;
    r.x = ctx.pick(i16, &.{ 0, 1, -1, 320, -320, 0x3C0, 0x100, 32767, -32768, 63, 64, -64 });
    r.y = ctx.pick(i16, &.{ 0, 1, -1, 256, -256, 0x100, 255, 32767, -32768 });
    r.w = ctx.pick(i16, &.{ 0, 1, -1, 2, -3, -5, 320, 32767, -32768 });
    r.h = ctx.pick(i16, &.{ 0, 1, -1, 2, 3, 16, 255, 256, 32767, -32768 });
    if (obj.scrollStep != 0 and ctx.chance(3)) {
        // frame + scrollStep lands exactly on rect.h: `frame >= rect.h` sees equal sides.
        r.h = ctx.edge(i16, &.{ 1, 2, 16, 100 });
        obj.frame = @truncate(@as(u32, @bitCast(@as(i32, r.h) -% obj.scrollStep)));
    }

    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    const next: u8 = obj.tick +% 1;
    if (next <= obj.delay) return 0;
    if (obj.scrollStep == 0) return 1;
    const f: u8 = @truncate(@as(u32, @bitCast(@as(i32, obj.frame) +% obj.scrollStep)));
    const after: i32 = if (@as(i32, f) >= obj.rect.h) 0 else @as(i32, f);
    return if (after != 0) 2 else 3;
}
