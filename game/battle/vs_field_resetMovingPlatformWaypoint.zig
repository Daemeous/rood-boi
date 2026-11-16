//! Room section 12 path entries: restarts one entry at a step (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_resetMovingPlatformWaypoint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Restarts room section 12 entry arg0 at step arg1 (-1: its stored step) and sets its position from that step.
pub fn vs_field_resetMovingPlatformWaypoint(arg0: c_int, arg1: c_int) callconv(.c) void {
    const sections = psx.get(v.g.vs_battle_roomData().section12);
    if (sections == null) return;

    const s0 = psx.at(sections.?, arg0);
    var target: c_int = arg1;
    if (target == -1) target = @as(c_int, psx.getBits(s0, "unk4_8"));
    if (target < 0) return;

    psx.setBits(s0, "unk0_9", 0);
    psx.setBits(s0, "unk4_1", 0);
    psx.setBits(s0, "unk0_27", psx.conv(u2, target));
    psx.setBits(s0, "unk0_14", psx.conv(u2, target));
    psx.setBits(s0, "unk0_29", psx.conv(u3, target));
    psx.setBits(s0, "unk4_16", 0);
    psx.setBits(s0, "unk4_0", 0);

    s0.unkA8 = s0.unk8[psx.getBits(s0, "unk0_27")].vx;
    s0.unkAC = s0.unk8[psx.getBits(s0, "unk0_27")].vy;
    s0.unkB0 = s0.unk8[psx.getBits(s0, "unk0_27")].vz;

    if (psx.getBits(s0, "unk0_14") == psx.getBits(s0, "unk0_10")) {
        psx.setBits(s0, "unk0_26", 0);
        psx.setBits(s0, "unk0_27", psx.conv(u2, @as(c_int, psx.getBits(s0, "unk0_27")) -% 1));
    } else {
        psx.setBits(s0, "unk0_26", 1);
        psx.setBits(s0, "unk0_27", psx.conv(u2, @as(c_int, psx.getBits(s0, "unk0_27")) +% 1));
    }

    const temp = c.vs_field_findRoomGeometryGroup(@as(c_int, psx.getBits(s0, "unk0_0")));
    if (temp != null) {
        temp[1] = @divTrunc(s0.unkA8, 4096);
        temp[2] = -%@divTrunc(s0.unkAC, 4096);
        temp[3] = @divTrunc(s0.unkB0, 4096);
    }
}
