//! Loads a weapon model's material palette into its CLUT slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: _loadMaterialPalette (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets a weapon model's material and copies its palette into the model's CLUT slot.
pub fn loadMaterialPalette(index: c_int, material: c_int) callconv(.c) void {
    const model = psx.at(v.g.vs_battle_wepModels(), index).*.?;

    var offset: c_int = 32;
    psx.setBits(model, "material", psx.conv(u4, material));
    const palette: c_int = @as(c_int, psx.at(v.g._weaponMaterialPaletteMap(), material).*);

    const double = psx.getBits(model, "isDoubleClut") != 0;
    if (!double) {
        offset = 16;
    }
    const count: c_int = if (double) 64 else 32;

    c.vs_main_memcpy(
        psx.at(&model.unk440, offset),
        psx.at(&model.palettes, palette),
        psx.conv(c_uint, count *% 2),
    );
    c.vs_main_memcpy(&model.unk500, &model.unk440, @as(c_uint, @sizeOf(@TypeOf(model.unk500))));
    const slot: c_int = (@as(c_int, model.clutSlotOffset) >> 1) +% 4;
    const colorOffset: c_int = ((@as(c_int, model.clutSlotOffset) & 1) *% 48) +% 160;
    c.vs_main_loadClut(
        &model.unk500,
        psx.conv(c_uint, slot),
        psx.conv(c_uint, colorOffset),
        psx.conv(c_uint, offset +% count),
    );
}
