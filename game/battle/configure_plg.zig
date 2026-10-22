//! Stores the PLG load context (lba, size) and the entry point count
//! (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: vs_battle_configurePlg (src/BATTLE/BATTLE.PRG/6F014.c)
/// Records the start sector and size of the PLG being loaded and its entry point count.
pub fn configurePlg(lba: c_int, size: c_int, entryPointCount: c_int) callconv(.c) void {
    v.g._plgLoadContext().lba = psx.conv(u16, lba);
    v.g._plgLoadContext().size = psx.conv(u16, size);
    v.g._plgEntryPointCount().* = entryPointCount;
}
