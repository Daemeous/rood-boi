//! Advances a GIM image loader (gim_t, three records in a row) through its states: the picture data is placed, the CLUT is loaded, and the images are drawn and released.
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_render_updateGimImageSet (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs one step of the GIM loader state machine on the loader at vs_render_gimLayers.
pub fn vs_render_updateGimImageSet() callconv(.c) void {
    const gim: *v.gim_t = v.g.vs_render_gimLayers().* orelse return;
    const second = psx.at(gim, 1);
    const third = psx.at(gim, 2);
    var i: c_int = 0;
    var j: c_int = 0;
    var count: c_int = 0;
    var width: c_int = 0;
    var tiles: c_int = 0;
    var height: c_int = 0;

    switch (psx.getBits(gim, "unk0_6")) {
        0 => {},
        1 => {
            const slot = psx.get(gim.cdQueueSlot).?;
            if (slot.state != 4) return;
            c.vs_main_freeCdQueueSlot(psx.cptr(c.vs_main_CdQueueSlot, slot));

            const data = psx.get(gim.data).?;
            count = @as(c_int, psx.at(data, 0).*);
            width = count & 0xFF;
            j = count >> 8;
            count = width *% j +% 4;
            psx.byteAt(u8, gim, 4).* = psx.conv(u8, width);
            psx.byteAt(u8, gim, 5).* = psx.conv(u8, j);
            // The C copies up to count entries, past the end of imageTable.
            i = 0;
            while (i < count) : (i +%= 1) {
                psx.at(&gim.imageTable, i).* = psx.at(data, i).*;
            }

            if (psx.at(data, 3).* != 0) {
                width = @as(c_int, psx.at(data, count).*);
                tiles = width >> 8;
                psx.byteAt(u8, second, 4).* = psx.conv(u8, width);
                psx.byteAt(u8, second, 5).* = psx.conv(u8, tiles);
                width = (width & 0xFF) *% tiles +% 4;
                i = 0;
                while (i < width) : (i +%= 1) {
                    psx.at(&second.imageTable, i).* = psx.at(data, i +% count).*;
                }
                count +%= width;

                if (psx.at(data, 3).* == 2) {
                    width = @as(c_int, psx.at(data, count).*);
                    tiles = width >> 8;
                    psx.byteAt(u8, third, 4).* = psx.conv(u8, width);
                    psx.byteAt(u8, third, 5).* = psx.conv(u8, tiles);
                    width = (width & 0xFF) *% tiles +% 4;
                    i = 0;
                    while (i < width) : (i +%= 1) {
                        psx.at(&third.imageTable, i).* = psx.at(data, i +% count).*;
                    }
                    count +%= width;
                }
            }

            count = (count +% 1) & 0xFFFE;
            width = (@as(c_int, gim.unk2) -% 24) *% 64;
            c.vs_battle_renderImage(((width +% 512) | 0x1FF0000), psx.at(data, count), 0x10040);

            j = 0;
            while (j < 3) : (j +%= 1) {
                const current = psx.at(gim, j);
                switch (current.imageTable[2]) {
                    0 => {
                        psx.setBits(current, "unk0_2", 0);
                        current.unkE = psx.conv(i16, psx.at(data, count +% j *% 16).*);
                    },
                    1 => {
                        psx.setBits(current, "unk0_2", 1);
                        current.unkE = psx.conv(i16, psx.at(data, count +% 64).*);
                    },
                    2 => {
                        psx.setBits(current, "unk0_2", 1);
                        current.unkE = psx.conv(i16, psx.at(data, count +% 320).*);
                        psx.setBits(current, "unk0_12", 1);
                        c.vs_battle_renderImage(((width +% 512) | 0x1FF0000), psx.at(data, count +% 320), 0x10100);
                    },
                    else => {},
                }
            }

            c.vs_main_loadClut(psx.at(data, count +% 64), 3, 0, 256);

            i = @as(c_int, psx.at(data, 1).*);
            height = i >> 8;
            i &= 0xFF;
            j = @divTrunc(i +% 16, 17);
            psx.setBits(gim, "unk0_9", psx.conv(u3, j));
            psx.setBits(second, "unk0_9", psx.conv(u3, j));
            psx.setBits(third, "unk0_9", psx.conv(u3, j));
            height *%= 16;
            count +%= height;
            _ = c.vs_render_reserveTexturePages(@as(c_uint, gim.unk2), @as(c_uint, psx.getBits(gim, "unk0_9")), 6);

            while (i > 0) {
                c.vs_battle_renderImage(
                    ((width +% 512) | 0x1000000),
                    psx.at(data, count),
                    if (i >= 17) 0xFF0040 else (((i *% 15) << 16) | 64),
                );
                i -%= 17;
                count +%= 0x3FC0;
                width +%= 64;
            }
            psx.setBits(gim, "unk0_6", 2);
        },
        2 => {
            c.vs_main_freeHeapR(@ptrCast(psx.get(gim.data)));
            j = 0;
            while (j < 3) : (j +%= 1) {
                psx.setBits(psx.at(gim, j), "unk0_1", 0);
            }
            psx.setBits(gim, "unk0_6", 3);
        },
        3 => {
            j = 0;
            while (j < 3) : (j +%= 1) {
                if (psx.getBits(psx.at(gim, j), "unk0_0") != 0) {
                    c.vs_render_drawGimLayer(j);
                }
            }
        },
        else => {},
    }
}
