//! zig_diff spec: func_800D5904 -> func_800D5904.func_800D5904 (5BF94.c).
//! Inputs: a command over 32 random words in a script in a 64 KiB arena
//! block (an entry index up to 63 reaches past unk1C's 16), its index
//! 0..31, the 9-bit frame equal half the time (an unsigned compare: a
//! negative frame never matches); kinds 0..3, the P-file's sound 0 or not.
//! vs_main_panSfx gets the C's stack position (a tag): its stub logs it.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5904));

pub const classes = [_][:0]const u8{ "other-frame", "stop", "play", "pan" };

var log: ?*[3]i16 = null;

pub const stubs = struct {
    pub fn vs_main_panSfx(ctx: *zd.Ctx, bank: anytype, id: anytype, position: anytype) void {
        _ = ctx;
        _ = bank;
        _ = id;
        const p: *v.SVECTOR = @ptrCast(@alignCast(position));
        log.?.* = .{ p.vx, p.vy, p.vz }; // the pad is uninitialised in the C
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    const s: *v.D_800F53B8_t = @ptrCast(@alignCast(ctx.allocBytes(0x10000, 16)));
    const cmd = ctx.alloc(v.func_800D5780_t);
    const words: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(32 * 4, 16)));
    psx.put(&cmd.unk0, @as(*u32, @ptrCast(words)));
    cmd.unk6 = ctx.range(i16, 0, 31);
    const t4 = ctx.alloc(v.D_800F53B8_t4);
    psx.put(&s.unkD1C.unk30, t4);
    t4.unk2 = ctx.pick(i16, &.{ 0, 1, 511, -1 });
    const k: usize = @intCast(cmd.unk6);
    if (ctx.random.boolean()) words[k] = (words[k] & ~@as(u32, 0x1FF)) | (@as(u32, @as(u16, @bitCast(t4.unk2))) & 0x1FF);
    const pf = ctx.alloc(v.D_800F569C_t);
    pf.unkCC = ctx.pick(i32, &.{ 0, 0, 1 });
    v.g.D_800F569C().* = pf;
    return .{ s, cmd };
}

pub fn classify(args: Args) usize {
    const words: [*]u32 = @ptrCast(psx.get(args[1].?.unk0).?);
    const w = words[@intCast(args[1].?.unk6)];
    if ((w & 0x1FF) != @as(u32, @bitCast(@as(i32, psx.get(args[0].?.unkD1C.unk30).?.unk2)))) return 0;
    if ((w >> 18) & 3 == 2) return 1;
    return if ((w >> 20) & 3 == 0) 2 else 3;
}
