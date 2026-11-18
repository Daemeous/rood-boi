//! Releases the battle's pending heap entries and the room's section 14 transfer (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_releaseRoomEffects (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Frees the heap blocks of the pending entries, starts the section 14 transfer if one is waiting, and clears the count.
pub fn vs_field_releaseRoomEffects() callconv(.c) c_int {
    if (v.g.vs_field_roomEffectScriptCount().* == 0) return 0;

    // Guest addresses stay as their raw 4-byte values: they are not dereferenced
    // here, and the C never checks them for alignment.
    var i: c_int = 0;
    while (i < @as(c_int, v.g.vs_field_roomEffectScriptCount().*)) : (i +%= 1) {
        // i < 256 (the count is a u8), so i + 64 cannot overflow.
        const entry = &v.g.vs_field_roomEffectInstances()[@intCast(i +% 64)];
        if (entry.unk0 != 0 or psx.get(entry.unk4) != psx.get(entry.unk8)) {
            entry.unk0 = 0;
            c.vs_main_freeHeap(psx.get(entry.unk4));
        }
    }

    if (v.g.vs_field_roomEffectSoundId().* != -1) {
        const section14: usize = @intFromPtr(psx.get(v.g.vs_battle_roomData().section14));
        if (section14 != 0) {
            // Called through the C's pointer type taken as an unaligned
            // pointer: the guest address is passed as the C gives it.
            const Transfer = *const fn (c_int, ?*anyopaque, c_int) callconv(.c) void;
            const transfer: Transfer = @ptrCast(&c.vs_audio_stopBankSfx);
            transfer(0x200, @ptrFromInt(section14), @as(c_int, v.g.vs_field_roomEffectSoundId().*));
            v.g.vs_field_roomEffectSoundId().* = -1;
        }
    }

    v.g.vs_field_roomEffectScriptCount().* = 0;
    return 1;
}
