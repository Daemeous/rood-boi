//! zig_diff spec: func_800D7BF8 -> func_800D7BF8.func_800D7BF8 (6F014.c).
//! Inputs: the load context's lba and size (edge values), a file buffer that is
//! already allocated or not, and a CD queue slot that is busy or free. A busy
//! slot is a 128-byte object whose every 32-bit word holds a small value 0-15,
//! so whichever width or offset the `state` field has, it reads that value. Some
//! of the values equal the loaded state, which exercises the "loaded" arm.
//! The heap and queue allocators return NULL sometimes. The queue-slot stub
//! copies the request it is given into the arena, so an lba or size the C
//! passes wrong shows up in the state the harness compares.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7BF8));

pub const classes = [_][:0]const u8{
    "buf-null, slot-free",
    "buf-null, slot-busy",
    "buf-kept, slot-free",
    "buf-kept, slot-busy",
};

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        if (ctx.value(u8) < 64) return null;
        return @ptrCast(ctx.allocBytes(@as(usize, size), 16));
    }

    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, slot: anytype) ?*anyopaque {
        // Record the request (the lba and size _loadfile stored) in the arena,
        // which the harness compares: a wrong lba or size is then a mismatch.
        const req: [*]const u8 = @ptrCast(slot);
        const log = ctx.allocBytes(8, 8);
        @memcpy(log[0..8], req[0..8]);
        if (ctx.value(u8) < 32) return null;
        const p = ctx.allocBytes(128, 16);
        const bytes: []u8 = p[0..128];
        const k: u32 = ctx.value(u8) & 15;
        var i: usize = 0;
        while (i < 128) : (i += 4) std.mem.writeInt(u32, bytes[i..][0..4], k, .little);
        return @ptrCast(p);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const lc = v.g.vs_battle_pFileLoadContext();
    lc.lba = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
    lc.size = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });

    if (ctx.chance(2)) {
        v.g.vs_battle_pfileBuf().* = @ptrCast(ctx.allocBytes(16, 16));
    } else {
        v.g.vs_battle_pfileBuf().* = null;
    }

    const slot = psx.global(?*anyopaque, "D_800F568C");
    if (ctx.chance(2)) {
        const obj = ctx.allocBytes(128, 16);
        const bytes: []u8 = obj[0..128];
        const k: u32 = ctx.range(u8, 0, 15);
        var i: usize = 0;
        while (i < 128) : (i += 4) std.mem.writeInt(u32, bytes[i..][0..4], k, .little);
        slot.* = @ptrCast(obj);
    } else {
        slot.* = null;
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const buf_kept = v.g.vs_battle_pfileBuf().* != null;
    const slot_busy = psx.global(?*anyopaque, "D_800F568C").* != null;
    return (if (buf_kept) @as(usize, 2) else 0) + (if (slot_busy) @as(usize, 1) else 0);
}
