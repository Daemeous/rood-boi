//! An effect event's particle emission: the event's count of nodes taken
//! from the effect's pool, their start positions by the emitter shape, their
//! velocities, sizes, colours and lifetimes from the event's curves
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const gte = common.gte;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

const Node = v.func_800D2904_t;
const Scratch = v.func_800FA098_arg1;

fn vp(p: *v.VECTOR) [*c]c.VECTOR {
    return psx.cptr(c.VECTOR, p);
}

fn sp(p: *v.SVECTOR) [*c]c.SVECTOR {
    return psx.cptr(c.SVECTOR, p);
}

/// `x << n` as GCC computes it on an int (wrapping).
fn shl(x: c_int, comptime n: u5) c_int {
    return @bitCast(@as(c_uint, @bitCast(x)) << n);
}

/// A short field's store of an int (C truncation).
fn s16(x: c_int) i16 {
    return psx.conv(i16, x);
}

/// D_800F5330[i]: the effect's current curve samples.
fn curveAt(i: u8) c_int {
    return psx.at(v.g.D_800F5330(), i).*;
}

fn rand() c_int {
    return c.rand();
}

fn uniform(a: c_int, b: c_int) c_int {
    return c.vs_battle_randUniformInt(a, b);
}

/// RAND_UNIFORM_WIDE(a, b) (5BF94.c): a 31-bit draw (two rand()s) in [min, max].
fn uniformWide(a: c_int, b: c_int) c_int {
    if (a == b) return a;
    const lo = if (b < a) b else a;
    const hi = if (b < a) a else b;
    const low = rand();
    const r = low +% shl(rand(), 16);
    return @rem(r, hi -% lo +% 1) +% lo;
}

/// psx_get(node->next): null after the last.
fn next(node: *Node) ?*Node {
    return psx.get(node.next);
}

/// orig: func_800D2ADC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Emits effect event `event` of the effect `arg0` (every unkC0-curve frames):
/// takes the count (unkBC curve, capped by the pool) of nodes, places them by
/// the emitter shape (flags & 0x38: box, octants, cylinder, disc, ring of
/// unkCA points) around the origin (flags & 7: none, a vector, the bone
/// matrix's translation, the parent's position), aims their velocities
/// (flags & 0xE000: the bone matrix, a target, both, away from the origin),
/// and gives them angular speeds, sizes, a pivot (flags & 0x1C0), the colour
/// curves, the transparency curve (arg2, the event's, the parent's or 4) and
/// the blend mode (arg3, or the parent's).
pub fn func_800D2ADC(arg0: ?*v.D_800F53B8_t, event: c_int, arg2: c_int, arg3_in: c_int, parent: ?*v.func_800FB4C0_t) callconv(.c) void {
    const effect = arg0.?;
    var arg3 = arg3_in;
    var node: ?*Node = null;
    var prevX: c_int = 0; // set before it is read (the ring's interpolated modes)
    var prevZ: c_int = 0;
    const scratch: *Scratch = @ptrFromInt(0x1F8001D0);
    const def = psx.at(&psx.get(v.g.D_800F569C().*.?.block5Data).?.unk4, event);

    scratch.flags = psx.getBits(def, "flags");
    scratch.unk28 = psx.conv(u16, scratch.flags);
    scratch.unk168 = def.unkC8;
    scratch.unk16C = def.unkC9;

    if (arg2 != 0) {
        scratch.unk180 = psx.conv(u8, arg2);
    } else if (psx.getBits(def, "transparencyCurve") != 0) {
        scratch.unk180 = psx.getBits(def, "transparencyCurve");
    } else if (parent) |p| {
        scratch.unk180 = psx.getBits(p, "unk60");
    } else {
        scratch.unk180 = 4;
    }

    if (parent != null and arg3 == 0) {
        arg3 = @as(c_int, psx.getBits(parent.?, "unk60_8")) & 0xF;
    }

    const frame: c_int = psx.getBits(effect, "unk14_16");
    var sample: c_int = undefined;
    if (@rem(frame, c.func_800CFE1C(&def.unkC0, curveAt(def.unk16))) != 0) return;

    gte.zrtr();

    var matrix: *v.MATRIX = &psx.at(&effect.unk1C, scratch.unk168).unk38;
    var count = c.func_800CFE1C(&def.unkBC, curveAt(def.unk15));
    if (count +% v.g.D_800F55F8().* >= v.g.D_800F55F0().*) {
        count = v.g.D_800F55F0().* -% v.g.D_800F55F8().*;
    }

    var i: c_int = 0;
    while (i < count) : (i += 1) {
        const n: *Node = @ptrCast(@alignCast(c.func_800D27F0(@ptrCast(effect)).?));
        node = n;
        n.unk6A = psx.conv(u8, scratch.unk168);
        n.unk6B = psx.conv(u8, scratch.unk16C);
        c.func_800D6CF0(@ptrCast(&n.unk3C), def.unk1, def.unk0);
    }

    c.vs_battle_lerpVector(&def.unk18, curveAt(def.unk8), vp(&scratch.unk98));
    const first = node;

    switch (scratch.flags & 7) {
        1 => c.vs_battle_addVecToSvec(vp(&scratch.unk98), @ptrCast(v.g.D_800F5310()), vp(&scratch.unk98)),
        2 => {
            scratch.unk98.vx +%= matrix.t[0];
            scratch.unk98.vy +%= matrix.t[1];
            scratch.unk98.vz +%= matrix.t[2];
        },
        4 => if (parent) |p| {
            scratch.unk98.vx +%= p.unkC.vx >> 12;
            scratch.unk98.vy +%= p.unkC.vy >> 12;
            scratch.unk98.vz +%= p.unkC.vz >> 12;
        },
        else => {},
    }

    scratch.unk98.vx = shl(scratch.unk98.vx, 12);
    scratch.unk98.vy = shl(scratch.unk98.vy, 12);
    scratch.unk98.vz = shl(scratch.unk98.vz, 12);

    c.vs_battle_lerpVector(&def.unk24, curveAt(def.unk9), vp(&scratch.unkA8));
    node = first;
    sample = c.func_800CFE1C(&def.unk30, curveAt(def.unk17));
    c.SetRotMatrix(@ptrCast(matrix));
    gte.zrtr();

    const a = &scratch.unkA8;
    const d = &scratch.unkD8;
    const s = &scratch.unk2C;
    switch (scratch.flags & 0x38) {
        0 => {
            d.vx = (a.vx *% sample) >> 12;
            d.vy = (a.vy *% sample) >> 12;
            d.vz = (a.vz *% sample) >> 12;
            i = 0;
            while (i < count) : (i += 1) {
                var angle = rand();
                scratch.unk170 = c.rsin(angle);
                scratch.unk178 = c.rcos(angle);
                angle = rand();
                scratch.unk174 = c.rsin(angle);
                scratch.unk17C = c.rcos(angle);
                const mode = scratch.flags & 0x1800;
                if (mode == 0 or mode == 0x800 or mode == 0x1000) {
                    if (mode == 0) {
                        s.vx = s16(uniform(a.vx, d.vx));
                        s.vy = s16(uniform(a.vy, d.vy));
                        s.vz = s16(uniform(a.vz, d.vz));
                    } else {
                        c.vs_battle_vecToSvec(vp(a), sp(s));
                    }
                    s.vx = s16((((@as(c_int, s.vx) *% scratch.unk178) >> 12) *% scratch.unk17C) >> 12);
                    s.vy = s16((((@as(c_int, s.vy) *% scratch.unk178) >> 12) *% scratch.unk174) >> 12);
                    s.vz = s16((@as(c_int, s.vz) *% scratch.unk170) >> 12);
                }
                c.func_800D2A38(@ptrCast(scratch), @ptrCast(node));
                node = next(node.?);
            }
        },
        8 => {
            d.vx = (a.vx *% sample) >> 12;
            d.vy = (a.vy *% sample) >> 12;
            d.vz = (a.vz *% sample) >> 12;
            i = 0;
            while (i < count) : (i += 1) {
                switch (scratch.flags & 0x1800) {
                    0 => {
                        const angle = rand();
                        s.vx = s16(if (angle & 1 != 0) uniform(a.vx, d.vx) else uniform(-%a.vx, -%d.vx));
                        s.vy = s16(if (angle & 2 != 0) uniform(a.vy, d.vy) else uniform(-%a.vy, -%d.vy));
                        s.vz = s16(if (angle & 4 != 0) uniform(a.vz, d.vz) else uniform(-%a.vz, -%d.vz));
                    },
                    0x800 => switch (@rem(rand(), 6)) {
                        0, 1 => |face| {
                            s.vx = s16(if (face == 0) a.vx else -%a.vx);
                            s.vy = s16(uniform(a.vy, -%a.vy));
                            s.vz = s16(uniform(a.vz, -%a.vz));
                        },
                        2, 3 => |face| {
                            s.vy = s16(if (face == 2) a.vy else -%a.vy);
                            s.vx = s16(uniform(a.vx, -%a.vx));
                            s.vz = s16(uniform(a.vz, -%a.vz));
                        },
                        4, 5 => |face| {
                            s.vz = s16(if (face == 4) a.vz else -%a.vz);
                            s.vx = s16(uniform(a.vx, -%a.vx));
                            s.vy = s16(uniform(a.vy, -%a.vy));
                        },
                        else => {},
                    },
                    0x1000 => {
                        const corner = v.g.D_800EC328();
                        corner.* = (corner.* +% 1) & 7;
                        s.vx = s16(if (corner.* & 1 != 0) a.vx else -%a.vx);
                        s.vy = s16(if (corner.* & 2 != 0) a.vy else -%a.vy);
                        s.vz = s16(if (corner.* & 4 != 0) a.vz else -%a.vz);
                    },
                    else => {},
                }
                c.func_800D2A38(@ptrCast(scratch), @ptrCast(node));
                node = next(node.?);
            }
        },
        16 => {
            d.vx = (a.vx *% sample) >> 12;
            d.vy = a.vy;
            d.vz = (a.vz *% sample) >> 12;
            i = 0;
            while (i < count) : (i += 1) {
                const angle = rand();
                scratch.unk170 = c.rsin(angle);
                scratch.unk178 = c.rcos(angle);
                const mode = scratch.flags & 0x1800;
                if (mode == 0 or mode == 0x800 or mode == 0x1000) {
                    if (mode == 0) {
                        s.vx = s16(uniform(a.vx, d.vx));
                        s.vz = s16(uniform(a.vz, d.vz));
                    } else {
                        s.vx = s16(a.vx);
                        s.vz = s16(a.vz);
                    }
                    s.vy = s16(c.vs_gte_rsqrt(uniformWide(a.vy *% a.vy, 0)));
                    s.vx = s16(@divTrunc(((@as(c_int, s.vx) *% scratch.unk178) >> 12) *% s.vy, a.vy));
                    s.vz = s16(@divTrunc(((@as(c_int, s.vz) *% scratch.unk170) >> 12) *% s.vy, a.vy));
                }
                c.func_800D2A38(@ptrCast(scratch), @ptrCast(node));
                node = next(node.?);
            }
        },
        24, 32 => |shape| {
            d.vx = (a.vx *% sample) >> 12;
            d.vz = (a.vz *% sample) >> 12;
            i = 0;
            while (i < count) : (i += 1) {
                const angle = rand();
                scratch.unk170 = c.rsin(angle);
                scratch.unk178 = c.rcos(angle);
                const mode = scratch.flags & 0x1800;
                if (mode == 0 or mode == 0x800 or mode == 0x1000) {
                    if (mode == 0) {
                        s.vx = s16(c.vs_gte_rsqrt(uniformWide(a.vx *% a.vx, d.vx *% d.vx)));
                        s.vz = s16(c.vs_gte_rsqrt(uniformWide(a.vz *% a.vz, d.vz *% d.vz)));
                    } else {
                        s.vx = s16(a.vx);
                        s.vz = s16(a.vz);
                    }
                    if (shape == 24) {
                        s.vx = s16((@as(c_int, s.vx) *% scratch.unk178) >> 12);
                        s.vy = s16(uniform(a.vy, 0));
                        s.vz = s16((@as(c_int, s.vz) *% scratch.unk170) >> 12);
                    } else {
                        s.vy = 0;
                        s.vx = s16((@as(c_int, s.vx) *% scratch.unk178) >> 12);
                        s.vz = s16((@as(c_int, s.vz) *% scratch.unk170) >> 12);
                    }
                }
                c.func_800D2A38(@ptrCast(scratch), @ptrCast(node));
                node = next(node.?);
            }
        },
        40, 48 => |shape| {
            if (shape == 40) {
                scratch.unk184 = 4;
                scratch.unk188 = 4;
            } else {
                scratch.unk184 = def.unkCA;
                if (scratch.unk184 == 0) scratch.unk184 = 1;
                scratch.unk188 = def.unkCB;
                if (scratch.unk188 == 0) scratch.unk188 = scratch.unk184;
            }
            const step = @divTrunc(@as(c_int, 0x1000), scratch.unk184);
            const point = v.g.D_800EC328();
            const mode = scratch.flags & 0x1800;
            if (mode != 0x1000) {
                point.* = @rem(point.*, scratch.unk188);
                const angle = point.* *% step +% 0x200;
                prevX = (c.rcos(angle) *% a.vx) >> 12;
                prevZ = (c.rsin(angle) *% a.vz) >> 12;
            }
            i = 0;
            while (i < count) : (i += 1) {
                const range = 0x1194 -% sample;
                point.* = @rem(point.* +% 1, scratch.unk188);
                const angle = point.* *% step +% 0x200;
                const x = (c.rcos(angle) *% a.vx) >> 12;
                const z = (c.rsin(angle) *% a.vz) >> 12;
                if (mode == 0x1000) {
                    s.vx = s16(x);
                    s.vz = s16(z);
                } else {
                    var value = rand() >> 3;
                    s.vx = s16((((x -% prevX) *% value) >> 12) +% prevX);
                    s.vz = s16((((z -% prevZ) *% value) >> 12) +% prevZ);
                    prevX = x;
                    prevZ = z;
                    if (mode != 0x800) {
                        value = c.SquareRoot12(((rand() *% range) >> 15) +% sample);
                        if (value > 0x1000) value = 0x1000;
                        s.vx = s16((@as(c_int, s.vx) *% value) >> 12);
                        s.vz = s16((@as(c_int, s.vz) *% value) >> 12);
                    }
                }
                s.vy = 0;
                c.func_800D2A38(@ptrCast(scratch), @ptrCast(node));
                node = next(node.?);
            }
        },
        else => {},
    }

    c.vs_battle_lerp2DVector(&def.unk94[2], curveAt(def.unkC), &scratch.unk118);
    scratch.unk118[0] = shl(scratch.unk118[0], 5);
    scratch.unk118[1] = shl(scratch.unk118[1], 5);
    c.vs_battle_lerpVector(&def.unk34[1], curveAt(def.unkB), vp(&scratch.unkC8));
    c.vs_battle_lerpVector(&def.unk34[0], curveAt(def.unkA), vp(&scratch.unkE8));
    node = first;

    const b = &scratch.unkB8;
    const aim = scratch.flags & 0xE000;
    if (aim == 0x4000 or aim == 0x6000) {
        const target = if (aim == 0x4000) &effect.unkD1C.unkC else &effect.unkD1C.unk24;
        if (target.unk0 == 4) {
            const bytes: *const [4]u8 = @ptrCast(&target.unk4);
            c.func_800A1AF8(bytes[0], bytes[2], sp(s), 0);
        } else {
            s.* = @as(*align(1) const v.SVECTOR, @ptrCast(&target.unk4)).*;
        }
        c.vs_battle_svecToVec(sp(s), vp(&scratch.unk34));
        scratch.unk44.vx = scratch.unk98.vx >> 12;
        scratch.unk44.vy = scratch.unk98.vy >> 12;
        scratch.unk44.vz = scratch.unk98.vz >> 12;
        c.vs_battle_lookAt(vp(&scratch.unk44), vp(&scratch.unk34), @ptrCast(&scratch.unk64));
        matrix = &scratch.unk64;
    }
    switch (aim) {
        0, 0x4000, 0x6000, 0x8000 => {
            i = 0;
            while (i < count) : (i += 1) {
                const e = &scratch.unkE8;
                const r = &scratch.unkC8;
                s.vx = s16(e.vx +% uniform(r.vx, -%r.vx));
                s.vy = s16(e.vy +% uniform(r.vy, -%r.vy));
                s.vz = s16(e.vz +% uniform(r.vz, -%r.vz));
                _ = c.RotMatrix_gte(sp(s), @ptrCast(scratch));
                gte.SetRotMatrix(scratch);
                psx.byteAt(c_int, s, 0).* = 0;
                s.vz = 0x1000;
                c.RotTrans(sp(s), vp(b), &b.pad);
                if (aim == 0x8000) {
                    gte.SetRotMatrix(matrix);
                    c.vs_battle_vecToSvec(vp(b), sp(s));
                    c.RotTrans(sp(s), vp(b), &b.pad);
                } else if (scratch.flags & 0x40000 != 0) {
                    c.vs_battle_vecToSvec(vp(b), sp(s));
                    gte.SetRotMatrix(matrix);
                    _ = c.ApplyRotMatrix(sp(s), vp(b));
                }
                scratch.unk120 = uniform(scratch.unk118[0], scratch.unk118[1]);
                const n = node.?;
                n.unk18[0] = (b.vx *% scratch.unk120) >> 12;
                n.unk18[1] = (b.vy *% scratch.unk120) >> 12;
                n.unk18[2] = (b.vz *% scratch.unk120) >> 12;
                node = next(n);
            }
        },
        0x2000 => {
            i = 0;
            while (i < count) : (i += 1) {
                const n = node.?;
                b.vx = (scratch.unk98.vx -% n.unkC) >> 12;
                b.vy = (scratch.unk98.vy -% n.unk10) >> 12;
                b.vz = (scratch.unk98.vz -% n.unk14) >> 12;
                scratch.unk120 = uniform(scratch.unk118[0], scratch.unk118[1]);
                if (b.vx != 0 or b.vy != 0 or b.vz != 0) {
                    _ = c.VectorNormal(vp(b), vp(b));
                    n.unk18[0] = (b.vx *% scratch.unk120) >> 12;
                    n.unk18[1] = (b.vy *% scratch.unk120) >> 12;
                    n.unk18[2] = (b.vz *% scratch.unk120) >> 12;
                } else {
                    n.unk18[1] = scratch.unk120;
                    n.unk18[2] = 0;
                    n.unk18[0] = 0;
                }
                node = next(n);
            }
        },
        else => {},
    }

    c.vs_battle_lerpSvector(&def.unk34[2], curveAt(def.unkD), sp(&scratch.unkF8));
    c.vs_battle_lerpSvector(&def.unk34[3], curveAt(def.unkD), sp(&scratch.unk100));
    c.vs_battle_lerpSvector(&def.unk34[4], curveAt(def.unkE), sp(&scratch.unk108));
    c.vs_battle_lerpSvector(&def.unk34[5], curveAt(def.unkE), sp(&scratch.unk110));
    node = first;
    i = 0;
    while (i < count) : (i += 1) {
        const n = node.?;
        n.unk24[0] = uniform(scratch.unkF8.vx, scratch.unk100.vx);
        n.unk24[1] = uniform(scratch.unkF8.vy, scratch.unk100.vy);
        n.unk24[2] = uniform(scratch.unkF8.vz, scratch.unk100.vz);
        n.unk30[0] = uniform(scratch.unk108.vx, scratch.unk110.vx);
        n.unk30[1] = uniform(scratch.unk108.vy, scratch.unk110.vy);
        n.unk30[2] = uniform(scratch.unk108.vz, scratch.unk110.vz);
        node = next(n);
    }

    c.vs_battle_lerp2DVector(&def.unk94[0], curveAt(def.unk12), &scratch.unk124);
    c.vs_battle_lerp2DVector(&def.unk94[1], curveAt(def.unk13), &scratch.unk12C);
    c.vs_battle_lerp2DVector(&def.unk94[4], curveAt(def.unk14), &scratch.unk134);
    node = first;
    i = 0;
    while (i < count) : (i += 1) {
        const n = node.?;
        n.unk8 = s16(uniform(scratch.unk124[0], scratch.unk124[1]));
        n.unkA = s16(uniform(scratch.unk12C[0], scratch.unk12C[1]));
        n.lifetime = s16(uniform(scratch.unk134[0], scratch.unk134[1]));
        node = next(n);
    }

    c.vs_battle_lerpSvector(&def.unk34[6], curveAt(def.unkF), sp(&scratch.unk13C));
    c.vs_battle_lerpSvector(&def.unk34[7], curveAt(def.unk10), sp(&scratch.unk144));
    node = first;

    const pivot = &scratch.unk13C;
    const spread = &scratch.unk144;
    switch (scratch.flags & 0x1C0) {
        0, 0x40 => {
            i = 0;
            while (i < count) : (i += 1) {
                const n = node.?;
                n.unk62 = .{ pivot.vx, pivot.vy, pivot.vz };
                node = next(n);
            }
        },
        0xC0 => {
            i = 0;
            while (i < count) : (i += 1) {
                const n = node.?;
                const t = &psx.at(&effect.unk1C, scratch.unk16C).unk38.t;
                // BUG in the original: x's spread is [vx, -vy], not [vx, -vx].
                n.unk62[0] = s16(t[0] +% pivot.vx +% uniform(spread.vx, -%@as(c_int, spread.vy)));
                n.unk62[1] = s16(t[1] +% pivot.vy +% uniform(spread.vy, -%@as(c_int, spread.vy)));
                n.unk62[2] = s16(t[2] +% pivot.vz +% uniform(spread.vz, -%@as(c_int, spread.vz)));
                node = next(n);
            }
        },
        0x80, 0x140 => {
            i = 0;
            while (i < count) : (i += 1) {
                const n = node.?;
                n.unk62[2] = 0;
                n.unk62[1] = 0;
                n.unk62[0] = 0;
                node = next(n);
            }
        },
        else => {},
    }

    c.vs_battle_lerp2DVector(&def.unk94[3], curveAt(def.unk11), &scratch.unk14C);
    node = first;
    scratch.unk154.@"packed" = psx.byteAt(i32, &def.rCurve, 0).*;
    // The PC C skips this read when no node was taken (count 0).
    if (count > 0) scratch.unk154.fields.age = node.?.unk74.fields.age;
    i = 0;
    while (i < count) : (i += 1) {
        const n = node.?;
        n.unk5C = @bitCast(scratch.flags);
        const value = uniform(scratch.unk14C[0], scratch.unk14C[1]);
        n.unk74.@"packed" = scratch.unk154.@"packed";
        n.unk70 = value;
        psx.byteAt(u16, &n.endEvent, 0).* = psx.byteAt(u16, &def.unk2, 0).*;
        psx.setBits(n, "unk60_0", scratch.unk180);
        psx.setBits(n, "unk60_8", psx.conv(u4, arg3));
        node = next(n);
    }
}
