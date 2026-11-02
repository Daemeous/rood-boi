//! zig_diff spec: func_800CCA90 -> func_800CCA90.func_800CCA90 (5BF94.c).
//! Inputs: image 0..2 of D_800EB9BC (arena records), its brightness 0, 0x80
//! or other, its mode 0..3 (D_800EC25C has four drawers, each set to a spec
//! drawer that logs its call), its flags random; the OT bases in vs_scratch
//! arena blocks. pc_classic_site gets the C's stack record (a tag): its stub
//! copies the record into an arena log; pc_opening_image_ready is a stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CCA90));

pub const classes = [_][:0]const u8{ "hidden", "mode-2", "front", "menu" };

var log: ?*[6]u64 = null;

fn drawer(gim: ?*v.gim_t, clut: c_int, ot: [*c]c_uint) callconv(.c) void {
    log.?[0] = @intFromPtr(gim);
    log.?[1] = @as(u32, @bitCast(clut));
    log.?[2] = @intFromPtr(ot);
}

pub const stubs = struct {
    pub fn pc_classic_site(ctx: *zd.Ctx, kind: anytype, record: anytype, x: anytype, y: anytype, first: anytype, end: anytype) void {
        _ = ctx;
        _ = kind;
        _ = x;
        _ = y;
        _ = first;
        _ = end;
        const r: *c.pc_classic_image = @ptrCast(@alignCast(record));
        log.?[3] = (@as(u64, @as(u32, @bitCast(r.image))) << 32) | @as(u32, @bitCast(r.mode));
        log.?[4] = (@as(u64, @as(u32, @bitCast(r.clut))) << 32) | @as(u32, @bitCast(r.slot));
        log.?[5] = @intFromPtr(r.gim);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(48, 16)));
    const images: [*]v.gim_t = @ptrCast(@alignCast(ctx.allocBytes(3 * @sizeOf(v.gim_t), 16)));
    v.g.D_800EB9BC().* = &images[0];
    const index = ctx.range(c_int, 0, 2);
    const gim = &images[@intCast(index)];
    gim.unk3 = ctx.pick(u8, &.{ 0, 0x80, 0x80, 1 });
    psx.setBits(gim, "unk0_3", ctx.range(u3, 0, 3));
    for (v.g.D_800EC25C()) |*d| d.* = @ptrCast(&drawer);
    const scratch = v.g.vs_scratch();
    psx.put(&scratch.unk4, @as(*anyopaque, @ptrCast(ctx.allocBytes(0x2000, 16))));
    psx.put(&scratch.unk8, @as(*anyopaque, @ptrCast(ctx.allocBytes(0x20, 16))));
    return .{index};
}

pub fn classify(args: Args) usize {
    const gim = psx.at(v.g.D_800EB9BC().*.?, args[0]);
    if (gim.unk3 == 0 and psx.getBits(gim, "unk0_3") != 2) return 0;
    if (gim.unk3 == 0) return 1;
    return if ((gim.unkC & 0x4000) != 0) 3 else 2;
}
