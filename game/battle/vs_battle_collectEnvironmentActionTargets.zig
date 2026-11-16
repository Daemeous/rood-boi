//! Rebuilds the environment target list for the current action from room sections or spawn points (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_collectEnvironmentActionTargets (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills the environment target list of vs_battle_actionState for the current action.
pub fn vs_battle_collectEnvironmentActionTargets() callconv(.c) void {
    const base = v.g.vs_battle_actionState().*.?;
    const list: *align(1) v.environmentTargetList = @ptrCast(&base.unk8);
    var points: [4]v.func_8008D2C0_t = undefined;
    var count: c_int = undefined;
    var i: c_int = undefined;

    list.count = 0;
    switch (@as(c_int, list.action)) {
        93, 241 => {
            const rooms: [*]v._mpdRoomSection9 = @ptrCast(c.func_8008E400(&count));
            i = 0;
            while (i < count) : (i +%= 1) {
                const r = psx.at(rooms, i);
                if (r.unk6 != 16) {
                    const t = psx.at(&list.targets, list.count);
                    t.kind = 3;
                    t.x = psx.conv(i16, @as(c_int, r.tileX) *% 128 +% 64);
                    const yArg0: c_int = (@as(c_int, r.tileX) << 7) | 64;
                    const yArg1: c_int = (@as(c_int, r.tileZ) << 7) | 64;
                    const yRaw: c_int = @as(c_int, c.vs_field_floorHeightAt(yArg0, yArg1));
                    t.y = psx.conv(i16, (yRaw << 17) >> 17);
                    t.z = psx.conv(i16, @as(c_int, r.tileZ) *% 128 +% 64);
                    t.radius = 128;
                    list.count +%= 1;
                }
            }
        },
        79 => {
            count = c.vs_field_collectMovingPlatformPositions(@ptrCast(&points));
            i = 0;
            while (i < count) : (i +%= 1) {
                const p = psx.at(&points, i);
                const t = psx.at(&list.targets, list.count);
                t.kind = 5;
                t.x = p.unk0;
                t.y = p.unk1;
                t.z = p.unk2;
                t.radius = 64;
                list.count +%= 1;
            }
        },
        else => {},
    }
    c.vs_battle_aimActionBehindSource(@ptrCast(&base.unk8));
}
