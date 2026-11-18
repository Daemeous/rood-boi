//! The effect package (P-file), decoded. The file is read whole into one heap buffer; this is the one place that knows its block chain
//! and each block's role. `bind` walks the chain and fills the global record `vs_effects_package` (D_800F569C_t) with a pointer to each
//! block's data and the few derived tables; the payload (particle definitions, curves, key tables, the timeline, sound banks) stays in the
//! buffer, raw, where the effect scripts and the audio side read it.
//!
//!   chain:   repeated { u32 meta; data }: the kind is meta & 0xFFFF0000, the block's byte size (meta word included) meta & 0xFFFF;
//!            kind 0 ends the chain. The data is the 4 bytes after the meta word.
//!   0x10000  block 1:  u16 dataOffset, count of tables and the offset of each (i32, from the block): the texture/UV data and its index tables
//!   0x20000  block 2:  the animation count and the animations
//!   0x30000  block 3:  count and u16 section offsets
//!   0x40000  curve block: count, per-curve sizes, dataOffset: the curves' data back to back (up to 32)
//!   0x50000  block 5:  count and the particle definitions
//!   0x80000  block 8:  the child-script data      0x90000  block 9     0xA0000  block 10     0xB0000  block 11
//!   0xC0000  block 12: the room sound bank (handed to the audio side)
//!   0xD0000  block 13: the room cutscene stream (handed to the audio side)
//! Any other kind is skipped by its size.
const psx = @import("common").psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// The C dereferences vs_effects_package unconditionally in a known block's parser: no guard, so `.?` traps where it would crash.
inline fn pkg() *v.D_800F569C_t {
    return v.g.vs_effects_package().*.?;
}

inline fn dataOf(cur: *align(1) v.p_file_t) *align(1) anyopaque {
    return @ptrCast(psx.byteAt(u8, cur, 4));
}

/// orig: vs_effects_bindEffectPackageBlocks (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Fills vs_effects_package from the chain of the package at `first`, in order, until the end marker.
pub fn bind(first: *align(1) v.p_file_t) void {
    var cur = first;
    while (true) {
        const meta: c_int = cur.meta;
        // meta & 0xFFFF0000 is unsigned in C (0xFFFF0000 is an unsigned constant).
        const kind: c_uint = @as(c_uint, @bitCast(meta)) & 0xFFFF_0000;
        const data = dataOf(cur);
        switch (kind) {
            0x0 => return,
            0x10000 => block1(pkg(), @ptrCast(data)),
            0x20000 => {
                const d = pkg();
                const b: *align(1) v.pFileBlock2 = @ptrCast(data);
                d.block2Count = b.animationCount;
                psx.put(&d.block2Data, b);
            },
            0x30000 => psx.put(&pkg().block3Data, data),
            0x40000 => curveBlock(pkg(), @ptrCast(data)),
            0x50000 => psx.put(&pkg().block5Data, data),
            0x80000 => psx.put(&pkg().block8Data, data),
            0x90000 => psx.put(&pkg().block9Data, data),
            0xA0000 => psx.put(&pkg().block10Data, data),
            0xB0000 => psx.put(&pkg().block11Data, data),
            0xC0000 => {
                const d = pkg();
                psx.put(&d.block12Data, data);
                _ = c.vs_audio_bindRoomSfxBank(data);
            },
            0xD0000 => {
                const d = pkg();
                psx.put(&d.block13Data, data);
                d.unkCC = c.vs_audio_bindRoomCutsceneStream(data);
            },
            else => {},
        }
        // Advance by the block size (meta & 0xFFFF, a byte count), wrapping as C pointer arithmetic does.
        const size: usize = @as(c_uint, @bitCast(meta)) & 0xFFFF;
        cur = @ptrFromInt(@intFromPtr(cur) +% size);
    }
}

fn block1(d: *v.D_800F569C_t, b: *align(1) v.pFileBlock1) void {
    d.block1TableCount = b.tableCount;
    // The data and the tables are offsets from the block: the u16 data offset zero-extends, a table's i32 offset is taken as a u32.
    const dataAddr: usize = @intFromPtr(b) +% @as(usize, b.dataOffset);
    psx.put(&d.block1Data, @as(*align(1) v.pFileBlock1Data, @ptrFromInt(dataAddr)));
    var i: c_int = 0;
    while (i < d.block1TableCount) : (i +%= 1) {
        const off: i32 = psx.at(&b.tableOffsets, i).*;
        const tableAddr: usize = @intFromPtr(b) +% @as(usize, psx.conv(u32, off));
        psx.put(psx.at(&d.block1Tables, i), @as(*align(1) v.pFileBlock1Indices, @ptrFromInt(tableAddr)));
    }
}

fn curveBlock(d: *v.D_800F569C_t, b: *align(1) v.pFileCurveBlock) void {
    psx.put(&d.curveBlock, b);
    var p = psx.byteAt(u8, b, @as(isize, b.dataOffset));
    var i: c_int = 0;
    // count is re-read each pass, as the C does; curves has 32 entries and the C does not check count.
    while (i < @as(c_int, b.count)) : (i +%= 1) {
        psx.put(psx.at(&d.curves, i), p);
        p = psx.byteAt(u8, p, @as(isize, psx.at(&b.curveSizes, i).*));
    }
}
