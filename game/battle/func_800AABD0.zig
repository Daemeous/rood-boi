//! Anchor and movement offset of a battle model (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800AABD0 (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets model `index`'s anchor to `pos` when `mode` is 0; otherwise stores its offset from the anchor, clearing the state when the offset is zero.
pub fn func_800AABD0(index: c_int, pos: ?*v.SVECTOR, mode: c_int) callconv(.c) void {
    const model = psx.at(v.g.D_800F4538(), index).*;
    if (model) |m| {
        if (mode == 0) {
            m.unk5C8 = 0;
            m.unk0.unk2C = pos.?.vx;
            m.unk0.unk2E = pos.?.vy;
            m.unk0.unk30 = pos.?.vz;
            return;
        }

        const p = pos.?;
        m.unk0.unk44.vx = psx.conv(i16, @as(c_int, p.vx) -% @as(c_int, m.unk0.unk2C));
        m.unk0.unk44.vy = psx.conv(i16, @as(c_int, p.vy) -% @as(c_int, m.unk0.unk2E));
        m.unk0.unk44.vz = psx.conv(i16, @as(c_int, p.vz) -% @as(c_int, m.unk0.unk30));

        // The C reads vx|vy as one int and tests vz << 16: all zero means the low 16 bits of vz are zero.
        const vz16: u16 = @bitCast(m.unk0.unk44.vz);
        if (m.unk0.unk44.vx == 0 and m.unk0.unk44.vy == 0 and vz16 == 0) {
            m.unk5C8 = 0;
            return;
        }

        m.unk5C8 = psx.conv(u16, mode);
    }
}
