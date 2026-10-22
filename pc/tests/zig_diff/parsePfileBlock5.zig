//! zig_diff spec: _parsePfileBlock5 -> parse_pfile_block5.parsePfileBlock5 (6F014.c).
//! Inputs: a random D_800F569C_t in the arena, pointed to by the global, and a
//! pFileBlock5 that is either null (the store writes psx_addr(NULL)) or an arena
//! object. The function has no branches; the value edges are null and non-null
//! block pointers, and the stored word must equal psx_addr(block) in the field
//! block5Data.raw (not a neighbouring field).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock5));

pub const classes = [_][:0]const u8{ "null-block", "arena-block" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // The C dereferences D_800F569C, so it must point at a valid object.
    const d = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = d;

    var block: ?*v.pFileBlock5 = null;
    if (!ctx.chance(4)) block = ctx.alloc(v.pFileBlock5);
    return .{block};
}

pub fn classify(args: Args) usize {
    if (args[0] == null) return 0;
    return 1;
}
