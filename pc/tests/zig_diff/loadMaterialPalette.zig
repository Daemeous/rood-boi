//! zig_diff spec: _loadMaterialPalette -> load_material_palette.loadMaterialPalette (30DB0.c).
//! Inputs: an index into vs_battle_wepModels (0-19, the one entry prepare sets)
//! pointing at a model in the arena, a material 0-7 (the 8-entry palette map),
//! map entries that are palette rows 0-8 plus the high values 127, 128 and 255
//! (read back through a signed cast by a mutation), any clutSlotOffset byte
//! (odd and even, so the 48-unit CLUT offset is exercised), and isDoubleClut
//! both ways. Palette rows past the table's seven overlap unk440 and unk500,
//! so the memcpy stub copies byte by byte in order, the same in both runs. The
//! model sits 40000 bytes into the arena so a negative or large palette index
//! reads inside the arena rather than before it.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadMaterialPalette));

pub const classes = [_][:0]const u8{ "single-clut", "double-clut", "palette-high" };

pub const stubs = struct {
    /// The real memcpy writes dest: the C reads unk500 back after the copy.
    /// Source and destination may overlap for out-of-range palette rows, so
    /// the copy runs forward one byte at a time (deterministic in both runs).
    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const n: usize = @intCast(sz);
        const d: [*]u8 = @ptrCast(dest.?);
        const s: [*]const u8 = @ptrCast(src.?);
        for (0..n) |i| d[i] = s[i];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // Room before the model for a signed palette index to read back into the arena.
    _ = ctx.allocBytes(40000, 16);
    const model = ctx.alloc(v.vs_battle_wepModels_t);
    const index = ctx.range(c_int, 0, 19);
    const material = ctx.range(c_int, 0, 7);
    v.g.vs_battle_wepModels()[@intCast(index)] = model;

    const map = v.g._weaponMaterialPaletteMap();
    for (0..8) |i| {
        map[i] = ctx.pick(u8, &.{ 0, 1, 6, 7, 8, 127, 128, 255 });
    }

    model.clutSlotOffset = ctx.int(u8);
    psx.setBits(model, "isDoubleClut", @intFromBool(ctx.chance(2)));
    return .{ index, material };
}

pub fn classify(args: Args) usize {
    const palette = v.g._weaponMaterialPaletteMap()[@intCast(args[1])];
    if (palette >= 128) return 2;
    const model = v.g.vs_battle_wepModels()[@intCast(args[0])].?;
    return if (psx.getBits(model, "isDoubleClut") != 0) 1 else 0;
}
