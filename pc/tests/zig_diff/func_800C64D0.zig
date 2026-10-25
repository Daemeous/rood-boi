//! zig_diff spec: func_800C64D0 -> func_800C64D0.func_800C64D0 (5BF94.c).
//! Inputs: a ring of u_long words in PS1 RAM (PS1 address 0 at 0x80000000,
//! `zd.low_ram`). arg0 is a word in that RAM at a 4-aligned guest address,
//! and the chain from it has 0 to 4 hops, each node's low 24 bits holding the
//! next node's guest address, the last one holding the address of the word
//! before arg0 (the C's `temp_t0`). Node top bytes are random, so the 0xFF000000
//! keep and the 0xFFFFFF masks are both observable. Some inputs start at
//! guest 0, where `arg0 - 1` wraps the 24-bit address to 0xFFFFFC. arg1 is an
//! arena int, so its psx_addr is a real host address with bits 16-23 set.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C64D0));

pub const classes = [_][:0]const u8{ "stop-at-once", "one-hop", "multi-hop", "wrap-start" };

const mask: u32 = 0x00FF_FFFF;
const top: u32 = 0xFF00_0000;

var last_hops: usize = 0;
var last_wrap: bool = false;

/// The RAM word at guest address `off`.
fn word(off: u32) [*c]c_uint {
    return @ptrFromInt(zd.low_ram + @as(usize, off));
}

fn used(offs: []const u32, cand: u32) bool {
    for (offs) |o| if (o == cand) return true;
    return false;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const wrap = ctx.chance(8);
    const hops: usize = if (ctx.chance(4)) 0 else ctx.range(usize, 1, 4);
    const x0: u32 = if (wrap) 0 else 4 * ctx.range(u32, 1, 0x1FFF);
    const temp: u32 = (x0 -% 4) & mask;

    // Chain nodes: offs[0] = arg0's word, offs[1..hops] distinct, none equal
    // to temp (so the walk stops only at the last node) and none equal to x0.
    var offs: [5]u32 = undefined;
    offs[0] = x0;
    var i: usize = 1;
    while (i <= hops) : (i += 1) {
        var cand: u32 = undefined;
        while (true) {
            cand = 4 * ctx.range(u32, 1, 0x1FFF);
            if (cand != temp and !used(offs[0..i], cand)) break;
        }
        offs[i] = cand;
    }

    // Each node keeps a random top byte; its link is the next node, the last
    // one links back to temp.
    i = 0;
    while (i <= hops) : (i += 1) {
        const link: u32 = if (i < hops) offs[i + 1] else temp;
        word(offs[i])[0] = (ctx.int(u32) & top) | link;
    }

    const a1: [*c]c_int = ctx.alloc(c_int);
    last_hops = hops;
    last_wrap = wrap;
    return .{ word(x0), a1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (last_wrap) return 3;
    return if (last_hops == 0) 0 else if (last_hops == 1) 1 else 2;
}
