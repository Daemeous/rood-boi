//! zig_diff spec: vs_battle_script_loadSoundFile2 -> script_load_sound_file2.script_loadSoundFile2 (4A0A8.c).
//! The C has no branches: it reads arg0[1], passes that byte to vs_main_loadSoundFile and returns 0.
//! Inputs: a 4-byte arena block as arg0 with random bytes (so arg0[1] takes every value, zero and
//! nonzero), and a random arg1 (unused by the C, but a port that reads it must differ). The callee
//! is a recording void stub, so the byte it receives is compared through the call trace.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_loadSoundFile2));

pub const classes = [_][:0]const u8{ "index-byte-zero", "index-byte-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Four bytes in the arena: arg0[1] is inside the block, arg0[2] too.
    const block: [*c]u8 = ctx.allocBytes(4, 1);
    const arg1: c_short = ctx.int(c_short);
    return .{ block, arg1 };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    return if (p[1] == 0) 0 else 1;
}
