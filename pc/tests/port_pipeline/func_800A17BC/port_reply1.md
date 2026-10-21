```zig
//! Battle unit 38C1C: actor bones and points (src/BATTLE/BATTLE.PRG/38C1C.c).
const psx = @import("common").psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

fn armatures(m: *v.D_800F4538_unk68) [*]v._armature_t {
    return @ptrCast(&m.armatures);
}

/// orig: func_800A17BC (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Copies a bone's matrix to `out` and stores the bone's length in `length`.
pub fn func_800A17BC(slot: c_int, bone: c_int, out: ?*anyopaque, length: [*c]c_int) callconv(.c) c_int {
    const matrix: ?*v.MATRIX = @ptrCast(@alignCast(c.func_800A1DE8(slot, bone, psx.cptr(c.MATRIX, v.g.D_800F49B8()))));
    const dest: *v.MATRIX = @ptrCast(@alignCast(out.?));
    dest.* = if (matrix) |m| m.* else psx.lowRam(v.MATRIX).*;
    const actor = psx.at(v.g.D_800F4538(), slot).*;
    if (actor == null) {
        const obj: ?*v.D_800F4538_t = @ptrCast(@alignCast(psx.at(v.g.D_800F45E0(), slot).*));
        if (obj == null) return 0;
        length[0] = psx.at(armatures(psx.get(obj.?.unk0.unk68).?), bone).unk0;
    } else if (bone == 0xFF) {
        length[0] = 0;
    } else if (bone & 0xF0 == 0x40) {
        const model = psx.at(v.g.vs_battle_wepModels(), slot *% 2).* orelse return 0;
        const offsets: *v.D_800F4538_unk68 = @ptrCast(@alignCast(psx.get(model.offsets).?));
        length[0] = psx.at(armatures(offsets), bone -% 0x3F).unk0;
    } else {
        length[0] = psx.at(armatures(psx.get(actor.?.unk0.unk68).?), bone).unk0;
    }
    return 0;
}
```
