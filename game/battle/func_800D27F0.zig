//! Takes a particle node from the free list (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D27F0 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Pops D_800F55F4 (func_800CE644 first when it is empty: the C then reads through NULL), clears it and pushes it on the script's list (unk18, count unk8).
pub fn func_800D27F0(script: ?*v.D_800F53B8_t) callconv(.c) ?*v.func_800D2904_t {
    const s = script.?;
    const free = v.g.D_800F55F4().*;
    if (free == null) {
        _ = c.func_800CE644(0x14);
    }
    const node = free.?; // the C goes on with NULL (a crash)
    c.func_800D6CCC(&node.unk3C);
    node.unk74.fields.age = 0xFF;
    psx.put(&node.previous, null);
    v.g.D_800F55F8().* +%= 1;
    v.g.D_800F55F4().* = psx.get(node.next);
    psx.put(&node.next, psx.get(s.unk18));
    if (psx.get(s.unk18)) |head| {
        psx.put(&head.previous, node);
    }
    psx.put(&s.unk18, node);
    s.unk8 +%= 1;
    return node;
}
