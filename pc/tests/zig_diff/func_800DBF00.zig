//! zig_diff spec: func_800DBF00 -> func_800DBF00.func_800DBF00 (73700.c).
//! Inputs: an unlink target `a` (a func_800E78F4_t in the arena or in PS1 low
//! RAM, so its pointer fields sometimes have the sign bit set) with a data
//! block in the arena, and the D_800F58BC global with its unk1C list head and
//! unk20 pointer. The list is an acyclic chain of 0-3 other nodes, with `a`
//! inside it, at its head, or outside it, and `a.unk1A0` is null or a valid
//! node. unk20 is `a`, null, or a chain node. The C makes no calls, so there
//! are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_73700");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DBF00));

pub const classes = [_][:0]const u8{
    "head-with-next",
    "head-alone",
    "list-head-is-arg",
    "found-in-list",
    "not-in-list",
};

const Node = v.func_800E78F4_t;

/// A node in the arena, or (sometimes) in PS1 low RAM, whose address has the sign bit set.
fn node(ctx: *zd.Ctx, low: bool, slot: *usize) *Node {
    if (low) {
        const p: *Node = @ptrFromInt(zd.low_ram + 0x2000 + slot.* * 0x480);
        slot.* += 1;
        return p;
    }
    return ctx.alloc(Node);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    var slot: usize = 0;

    const glob = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = glob;

    const a = node(ctx, ctx.chance(3), &slot);
    const data = ctx.alloc(v.func_800DEB10_t2);
    psx.put(&a.unk54, data);

    // Other nodes, then `a` inserted at a random position with probability 1/2.
    var chain: [5]*Node = undefined;
    const others = ctx.range(usize, 0, 3);
    var len: usize = others;
    for (0..others) |i| chain[i] = node(ctx, ctx.chance(3), &slot);
    const in_chain = ctx.chance(2);
    if (in_chain) {
        const at = ctx.range(usize, 0, len);
        var j = len;
        while (j > at) : (j -= 1) chain[j] = chain[j - 1];
        chain[at] = a;
        len += 1;
    }
    for (0..len) |i| {
        psx.put(&chain[i].unk1A0, if (i + 1 < len) chain[i + 1] else null);
    }

    if (!in_chain) {
        // `a` is outside the list: its successor is null or a node that is not reached from the head.
        const extra = node(ctx, ctx.chance(3), &slot);
        psx.put(&extra.unk1A0, null);
        psx.put(&a.unk1A0, if (ctx.chance(2)) null else extra);
    }

    // The list head: `a` itself sometimes (when it is outside the chain), else the first node.
    const head: ?*Node = if (!in_chain and ctx.chance(3)) a else if (len > 0) chain[0] else null;
    psx.put(&glob.unk1C, head);

    // The unk20 head: `a` about 37% of the time, null about 16%, else a chain node.
    const r = ctx.int(u8);
    const head20: ?*Node = if (r < 96) a else if (r < 128) null else if (len > 0) chain[ctx.range(usize, 0, len - 1)] else a;
    psx.put(&glob.unk20, head20);

    return .{a};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const glob = v.g.D_800F58BC().*.?;
    if (psx.get(glob.unk20) == a) return if (psx.get(a.unk1A0) != null) 0 else 1;
    if (psx.get(glob.unk1C) == a) return 2;
    var n = psx.get(glob.unk1C);
    while (n) |cur| : (n = psx.get(cur.unk1A0)) {
        if (psx.get(cur.unk1A0) == a) return 3;
    }
    return 4;
}
