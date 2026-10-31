//! zig_diff spec: func_800D4F00 -> func_800D4F00.func_800D4F00 (5BF94.c).
//! Inputs: a random D_800F53B8_t stream object (always present, since the
//! callee writes into it), and a D_800F569C_t whose block5Data points at an
//! arena block of 64 curve entries (the C indexes unk4[first & 0x3F]). The
//! three stream bytes come from the callee stub's ctx.value draws, so every
//! byte value (0, 63, 64, nibble edges, the top bits) occurs. The callee may
//! also repoint D_800F569C->block5Data to a second block, which the C reads
//! back after its three calls. The C has no branches; the value edges that
//! matter are the index's low six bits and the packed nibbles.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4F00));

pub const classes = [_][:0]const u8{ "swap-armed", "swap-off" };

var block: *v.pFileBlock5 = undefined;
var other: *v.pFileBlock5 = undefined;
var swap_block: bool = false;

/// A pFileBlock5 with room for count plus 64 curve entries (unk4 is [0]).
fn newBlock(ctx: *zd.Ctx) *v.pFileBlock5 {
    const bytes = ctx.allocBytes(@sizeOf(v.pFileBlock5) + 64 * @sizeOf(v.func_800FA098_arg0), 16);
    return @ptrCast(@alignCast(bytes));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const info = ctx.alloc(v.D_800F53B8_t);
    const holder = ctx.alloc(v.D_800F569C_t);
    block = newBlock(ctx);
    other = newBlock(ctx);
    psx.put(&holder.block5Data, block);
    v.g.D_800F569C().* = holder;
    swap_block = ctx.chance(3);
    return .{info};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (swap_block) 0 else 1;
}

pub const stubs = struct {
    /// Reads one stream byte. It may change the stream object's unk8 (not
    /// read back by the C) and may repoint the curve block, which the C
    /// reads after its three calls.
    pub fn func_800D5170(ctx: *zd.Ctx, p: anytype) u8 {
        const b: ?*v.D_800F53B8_t = @ptrCast(@alignCast(p));
        if (b) |o| {
            if (ctx.value(u1) == 1) o.unk8 = ctx.value(u8);
        }
        if (swap_block and ctx.value(u1) == 1) {
            if (v.g.D_800F569C().*) |d| psx.put(&d.block5Data, other);
        }
        return ctx.value(u8);
    }
};
