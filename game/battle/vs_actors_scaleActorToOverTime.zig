//! Anchor and movement offset of a battle model (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: vs_actors_scaleActorToOverTime (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Sets model `index`'s anchor to `pos` when `mode` is 0; otherwise stores its offset from the anchor, clearing the state when the offset is zero.
pub fn vs_actors_scaleActorToOverTime(index: c_int, pos: ?*v.SVECTOR, mode: c_int) callconv(.c) void {
    const model = psx.at(v.g.vs_actors_actorRecords(), index).*;
    if (model) |m| {
        if (mode == 0) {
            m.unk5C8 = 0;
            m.unk0.scaleX = pos.?.vx;
            m.unk0.scaleY = pos.?.vy;
            m.unk0.scaleZ = pos.?.vz;
            return;
        }

        const p = pos.?;
        m.unk0.scaleDelta.vx = psx.conv(i16, @as(c_int, p.vx) -% @as(c_int, m.unk0.scaleX));
        m.unk0.scaleDelta.vy = psx.conv(i16, @as(c_int, p.vy) -% @as(c_int, m.unk0.scaleY));
        m.unk0.scaleDelta.vz = psx.conv(i16, @as(c_int, p.vz) -% @as(c_int, m.unk0.scaleZ));

        // The C reads vx|vy as one int and tests vz << 16: all zero means the low 16 bits of vz are zero.
        const vz16: u16 = @bitCast(m.unk0.scaleDelta.vz);
        if (m.unk0.scaleDelta.vx == 0 and m.unk0.scaleDelta.vy == 0 and vz16 == 0) {
            m.unk5C8 = 0;
            return;
        }

        m.unk5C8 = psx.conv(u16, mode);
    }
}
