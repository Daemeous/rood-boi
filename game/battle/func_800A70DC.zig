//! Samples the floor under a battle actor at its eight neighbours and marks the blocked ones (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// Reads the floor attribute flag, (*D_800F49F0 >> 5) & 1, at the moment it is called.
fn floorAttribute() u8 {
    const word: c_uint = v.g.D_800F49F0().*.?[0];
    return psx.conv(u8, (word >> 5) & 1);
}

/// Floor height at offset (x, z) from the actor's position, as a 16-bit sample.
fn sampleHeight(pos: *v.SVECTOR, x: c_int, z: c_int) i16 {
    return psx.conv(i16, c.func_800A6EE8(psx.cptr(c.SVECTOR, pos), x, z, 0));
}

/// vs_battleRelativeSampleHeight: sample (sector + offset) & 7 minus y.
fn relativeSampleHeight(samples: *v.ActorCollisionSamples, sector: c_int, offset: c_int, y: c_int) c_int {
    const k: usize = @intCast((sector +% offset) & 7);
    return @as(c_int, samples.heights[k]) -% y;
}

/// orig: func_800A70DC (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Fills the scratchpad collision samples around an actor and flags the directions it cannot step into.
pub fn func_800A70DC(actor: ?*v.D_800F4538_t, diagonal: c_int) callconv(.c) void {
    const a = actor.?;
    const samples: *v.ActorCollisionSamples = @ptrFromInt(0x1F8003BC);

    psx.setBits(a, "unk5AC_12", 0);
    var i: c_int = 7;
    while (i >= 0) : (i -%= 1) {
        samples.blocked[@intCast(i)] = 0;
    }

    if ((psx.byteAt(c_uint, a, 0x5AC).* & 0x600) != 0) {
        i = 7;
        while (i >= 0) : (i -%= 1) {
            samples.heights[@intCast(i)] = 0;
        }
        return;
    }

    if (psx.getBits(&a.unk0, "unkA_3") == 2) {
        i = 0;
        while (i < 8) : (i += 1) {
            samples.heights[@intCast(i)] = a.unk0.position.vy;
        }
        return;
    }

    i = a.unk63C;
    const negativeRadius: c_int = -%i;

    // ROOD_PC: off the floor the tile lookup is NULL; the original reads PS1 address 0.
    const tile = c.func_800A8D64(psx.cptr(c.SVECTOR, &a.unk0.position), 0);
    const word: c_uint = if (tile != null) tile[0] else psx.lowRam(c_uint).*;
    samples.centerAttribute = psx.conv(u8, (word >> 5) & 1);

    const pos = &a.unk0.position;
    samples.heights[0] = sampleHeight(pos, 0, negativeRadius);
    samples.attributes[0] = floorAttribute();
    samples.heights[6] = sampleHeight(pos, i, 0);
    samples.attributes[6] = floorAttribute();
    samples.heights[2] = sampleHeight(pos, negativeRadius, 0);
    samples.attributes[2] = floorAttribute();
    const negativeDiagonal: c_int = -%diagonal;
    samples.heights[7] = sampleHeight(pos, diagonal, negativeDiagonal);
    samples.attributes[7] = floorAttribute();
    samples.heights[1] = sampleHeight(pos, negativeDiagonal, negativeDiagonal);
    samples.attributes[1] = floorAttribute();
    samples.heights[3] = sampleHeight(pos, negativeDiagonal, diagonal);
    samples.attributes[3] = floorAttribute();
    samples.heights[5] = sampleHeight(pos, diagonal, diagonal);
    samples.attributes[5] = floorAttribute();
    samples.heights[4] = sampleHeight(pos, 0, i);
    samples.attributes[4] = floorAttribute();

    var j: c_int = undefined;
    if (psx.getBits(&a.unk0, "unkA_0") == 3 and a.unk0.unk34.vy > 0) {
        j = @as(c_int, a.unk0.position.vy) -% 32;
    } else if (a.unk1848.unk8 == 0) {
        j = @as(c_int, a.unk0.position.vy) -% 64;
    } else {
        j = @as(c_int, a.unk0.position.vy) -% 96;
    }

    i = 0;
    while (i < 8) : (i += 1) {
        const idx: usize = @intCast(i);
        if (samples.heights[idx] > a.unk0.position.vy) {
            samples.heights[idx] = a.unk0.position.vy;
        }
        if ((samples.attributes[idx] == 0 or samples.centerAttribute == 0) and
            j >= @as(c_int, samples.heights[idx]))
        {
            samples.blocked[idx] = 1;
            psx.setBits(a, "unk5AC_12", 1);
        }
    }

    if (psx.getBits(&a.unk0, "unkA_0") != 0) {
        i = @divTrunc(@as(c_int, psx.conv(i16, a.unk1848.unk4)) +% 256, 512);
        j = 0;
        while (true) {
            const delta = relativeSampleHeight(samples, i, j -% 1, @as(c_int, a.unk0.position.vy));
            if (delta < -255) return;
            j +%= 1;
            if (delta >= -192) return;
            if (j >= 3) break;
        }
        j = 0;
        while (true) {
            const delta = relativeSampleHeight(samples, i, j +% 2, @as(c_int, a.unk0.position.vy));
            j +%= 1;
            if (delta < -192) return;
            if (j >= 5) break;
        }
        psx.at(&samples.heights, i).* = a.unk0.position.vy;
        psx.setBits(a, "unk5AC_12", 0);
    }
}
