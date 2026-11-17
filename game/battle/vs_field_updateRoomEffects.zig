//! Per-entry callbacks over the battle table vs_field_roomEffectInstances: the first 64 entries
//! call a handler chosen by kind, the next entries run a timed step and free
//! themselves when it finishes (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_updateRoomEffects (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Runs the kind-dispatched handlers of the first entries, then the timed steps of the next ones, freeing those that finish.
pub fn vs_field_updateRoomEffects() callconv(.c) void {
    const Handler = *const fn (?*v.vs_field_SwingEffectRecord) callconv(.c) c_int;

    var i: c_int = 0;
    while (i < @as(c_int, v.g.vs_field_textureEffectCount().*)) : (i +%= 1) {
        const e = &v.g.vs_field_roomEffectInstances()[@as(usize, @intCast(i))];
        if (e.unk0 != 0 and e.unk1 < 7 and e.unk2 != 0) {
            const handler: Handler = @ptrCast(v.g.vs_field_textureEffectHandlers()[e.unk1].?);
            e.unk0 = psx.conv(u8, handler(psx.get(e.unk4)));
        }
    }

    i = 0;
    while (i < @as(c_int, v.g.vs_field_roomEffectScriptCount().*)) : (i +%= 1) {
        const e = &v.g.vs_field_roomEffectInstances()[@as(usize, @intCast(i + 64))];
        if (e.unk0 != 0 and e.unk2 != 0) {
            e.unk0 = psx.conv(u8, c.vs_field_executeRoomEffectCommands(
                @ptrCast(@alignCast(psx.get(e.unk4))),
                @as(c_int, e.unk1),
                @ptrCast(e),
            ));
            if (e.unk0 == 0) {
                c.vs_main_freeHeap(psx.get(e.unk4));
                // psx_copy: the raw guest address of unk8 becomes unk4 (same field type, bitwise copy).
                e.unk4 = e.unk8;
            }
        }
    }
}
