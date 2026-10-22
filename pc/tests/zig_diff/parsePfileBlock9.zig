//! zig_diff spec: _parsePfileBlock9 -> parse_pfile_block9.parsePfileBlock9 (6F014.c).
//! Inputs: a D_800F569C_t in the arena, pointed to by the global, and a pFileBlock9
//! argument that is either null or a block allocated in the arena. The C has no
//! branches of its own: it stores the argument's guest address into
//! D_800F569C->block9Data.raw. The sizeof-wrapped assignment is unevaluated, so only
//! that one store happens. The null case covers the address of NULL, and the arena
//! case covers ordinary addresses. Arena addresses are below 4 GB, as psx_addr requires.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock9));

pub const classes = [_][:0]const u8{ "null-arg", "arena-arg" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = block;
    const arg: ?*v.pFileBlock9 = if (ctx.chance(6)) null else ctx.alloc(v.pFileBlock9);
    return .{arg};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
