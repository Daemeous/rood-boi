//! An actor's step collision against the map: probes the tiles ahead and
//! beside the move, accepts it, keeps one axis, or slides along a wall
//! corner (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

fn abs(x: c_int) c_int {
    return if (x >= 0) x else -%x;
}

fn word(actor: *v.D_800F4538_t, offset: isize) c_uint {
    return psx.byteAt(c_uint, actor, offset).*;
}

/// `func_800A8B34(&p, direction)`: the tile probe (it writes p's height and area).
fn tileProbe(p: *v.SVECTOR, direction: c_int) c_int {
    return c.func_800A8B34(@ptrCast(p), direction);
}

/// The height steps a probe at `vy` may climb from the actor's height.
fn climb(vy: i16, actor: *v.D_800F4538_t) c_int {
    const offset: c_int = @as(c_int, vy) - actor.unk0.position.vy;
    var steps: c_int = @divTrunc(offset, 64);
    if (offset & 0x3F != 0) steps -%= 1;
    steps = -%steps;
    if (steps <= 0) steps = 1;
    return steps;
}

fn allWithin(results: *const [5]c_int, limit: c_int) bool {
    return results[0] != 0xFF and results[1] <= limit and results[2] <= limit and results[3] <= limit and results[4] <= limit;
}

fn allBelow2(results: *const [5]c_int) bool {
    return results[0] != 0xFF and results[1] < 2 and results[2] < 2 and results[3] < 2 and results[4] < 2;
}

fn setV(p: *v.SVECTOR, comptime field: []const u8, value: c_int) void {
    @field(p, field) = psx.conv(i16, value);
}

/// The C's function-wide locals the goto tails share.
const Locals = struct {
    actor: *v.D_800F4538_t,
    motion: *v.SVECTOR,
    result: [*c]c_int,
    scratch: *v.D_1F8003BC_t,
    xResults: [5]c_int = undefined,
    zResults: [5]c_int = undefined,
    probeDirections: [2]c_int = undefined,
    probe: v.SVECTOR = undefined,
    opposite: v.SVECTOR = undefined,
    xProbe: v.SVECTOR = undefined,
    zProbe: v.SVECTOR = undefined,
    // Uninitialised in the original when the actor's tile flags skip the
    // tile lookup (the probe-only path then stores it): null here.
    probeTiles: [2]?[*]u32 = .{ null, null },
    radius: c_int,
    diagonal: c_int,
    direction: c_int = undefined,
    offset: c_int = undefined,
    diagonalOffset: c_int = undefined,
    crossOffset: c_int = undefined,
    crossDiagonalOffset: c_int = undefined,
    sideOffset: c_int = undefined,
    cross: c_int = undefined,
    crossDiagonal: c_int = undefined,
    cornerStep: c_int = undefined,
};

/// The C's `moved:` tail: the slide's move is taken.
fn moved(l: *Locals) c_int {
    l.result[0] = l.zResults[3];
    l.actor.unk17FE = psx.conv(u8, l.direction & 7);
    psx.setBits(l.actor, "unk5AC_13", 1);
    return l.offset;
}

/// The C's `acceptX:` tail.
fn acceptX(l: *Locals) c_int {
    l.motion.vz = 0;
    l.result[0] = l.xResults[0];
    v.g.D_800F4B08().* = l.xProbe;
    v.g.D_800F49F0().* = l.probeTiles[0];
    v.g.D_800F49F9().* = psx.conv(u8, l.probeDirections[0]);
    return 1;
}

/// The z axis alone is free: keep it (returns 2).
fn acceptZ(l: *Locals) c_int {
    l.motion.vx = 0;
    l.result[0] = l.zResults[0];
    v.g.D_800F4B08().* = l.zProbe;
    v.g.D_800F49F0().* = l.probeTiles[1];
    v.g.D_800F49F9().* = psx.conv(u8, l.probeDirections[1]);
    return 2;
}

fn signedSide(l: *Locals, negative: bool, positiveDirection: c_int, negativeDirection: c_int) void {
    l.direction = positiveDirection;
    if (negative) {
        l.offset = -%l.radius;
        l.direction = negativeDirection;
        l.diagonalOffset = -%l.diagonal;
    } else {
        l.offset = l.radius;
        l.diagonalOffset = l.diagonal;
    }
}

/// The C's `slideXProbe:` .. `slideXMove:`: the corner probes of a slide along x.
fn slideXProbe(l: *Locals) c_int {
    const a = l.actor;
    const pos = &a.unk0.position;
    l.direction -%= l.crossDiagonalOffset;
    setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.crossOffset +% l.offset);
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.cross);
    l.zResults[1] = tileProbe(&l.probe, l.direction);
    if (l.zResults[1] < l.scratch.unk40) l.zResults[1] = 0xFF;
    l.direction +%= l.crossDiagonalOffset;
    setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.crossOffset +% l.diagonalOffset);
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.cross +% l.crossDiagonal);
    l.zResults[2] = tileProbe(&l.probe, l.direction);
    if (l.zResults[2] < l.scratch.unk40) l.zResults[2] = 0xFF;
    l.direction +%= l.cornerStep;
    setV(&l.probe, "vx", pos.vx);
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.crossOffset +% l.sideOffset);
    l.zResults[3] = tileProbe(&l.probe, l.direction);
    if (l.zResults[3] < l.scratch.unk40) l.zResults[3] = 0xFF;
    v.g.D_800F4B08().* = l.probe;
    if (l.zResults[1] < 2 and l.zResults[2] < 2) {
        if (l.zResults[3] >= 2) return 3;
        setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.crossOffset);
        setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.cross +% l.sideOffset);
        l.zResults[0] = tileProbe(&l.probe, l.direction);
        if (l.zResults[0] >= 2) return 3;
        l.direction +%= l.crossDiagonalOffset;
        l.offset = 0;
    } else {
        if (l.zResults[3] >= 2) return 3;
        l.crossOffset = 0;
        l.offset = 2;
    }
    setV(l.motion, "vx", l.crossOffset);
    setV(l.motion, "vz", l.cross);
    return moved(l);
}

/// The C's `slideXBack:`.
fn slideXBack(l: *Locals) c_int {
    l.direction -%= 2;
    l.sideOffset = -%l.offset;
    l.crossDiagonal = l.diagonalOffset;
    l.cross = -%l.crossOffset;
    l.crossDiagonalOffset = -1;
    l.cornerStep = 3;
    return slideXProbe(l);
}

/// The C's `slideX:`: slide along a wall while moving mainly along x.
fn slideX(l: *Locals) c_int {
    const a = l.actor;
    const pos = &a.unk0.position;
    if (word(a, 8) & 0x70000 != 0) return 3;
    signedSide(l, l.motion.vx < 0, 6, 2);
    l.direction -%= 1;
    setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.motion.vx +% l.offset);
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.offset);
    l.zResults[0] = tileProbe(&l.probe, l.direction);
    l.direction +%= 2;
    l.opposite.vx = l.probe.vx;
    setV(&l.opposite, "vz", @as(c_int, pos.vz) -% l.offset);
    l.zResults[1] = tileProbe(&l.opposite, l.direction);
    if (l.zResults[0] == 0xFF and l.zResults[1] == 0xFF) return 3;

    l.crossOffset = @divTrunc(@as(c_int, l.motion.vx), 2);
    if (l.zResults[0] < 2) {
        if (l.zResults[1] < 2) {
            l.direction -%= 2;
            setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.motion.vx);
            setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.offset);
            l.zResults[2] = tileProbe(&l.probe, l.direction);
            l.direction +%= 2;
            l.opposite.vx = l.probe.vx;
            setV(&l.opposite, "vz", @as(c_int, pos.vz) -% l.offset);
            l.zResults[3] = tileProbe(&l.opposite, l.direction);
            if (l.zResults[2] == 0xFF and l.zResults[3] == 0xFF) return 3;
            if (l.zResults[2] > 0) return slideXBack(l);
            if (l.zResults[3] <= 0 and (l.zResults[0] != 0 or l.zResults[1] != 1)) {
                if (l.zResults[0] == 1 and l.zResults[1] == 1) return slideXBack(l);
                if (l.probe.vy <= l.opposite.vy) return slideXBack(l);
            }
        }
        l.sideOffset = l.offset;
        l.crossDiagonal = -%l.diagonalOffset;
        l.cross = l.crossOffset;
        l.crossDiagonalOffset = 1;
        l.cornerStep = -3;
        return slideXProbe(l);
    }
    if (l.zResults[1] >= 2) return 3;
    return slideXBack(l);
}

/// The C's `slideZProbe:` .. `slideZMove:`: the corner probes of a slide along z.
fn slideZProbe(l: *Locals) c_int {
    const a = l.actor;
    const pos = &a.unk0.position;
    l.direction -%= l.crossDiagonalOffset;
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.crossOffset +% l.offset);
    setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.cross);
    l.zResults[1] = tileProbe(&l.probe, l.direction);
    if (l.zResults[1] < l.scratch.unk40) l.zResults[1] = 0xFF;
    l.direction +%= l.crossDiagonalOffset;
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.crossOffset +% l.diagonalOffset);
    setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.cross +% l.crossDiagonal);
    l.zResults[2] = tileProbe(&l.probe, l.direction);
    if (l.zResults[2] < l.scratch.unk40) l.zResults[2] = 0xFF;
    l.direction +%= l.cornerStep;
    setV(&l.probe, "vz", pos.vz);
    setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.cross +% l.sideOffset);
    l.zResults[3] = tileProbe(&l.probe, l.direction);
    if (l.zResults[3] < l.scratch.unk40) l.zResults[3] = 0xFF;
    v.g.D_800F4B08().* = l.probe;
    if (l.zResults[1] < 2 and l.zResults[2] < 2) {
        if (l.zResults[3] >= 2) return 3;
        setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.cross +% l.sideOffset);
        setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.crossOffset);
        l.zResults[0] = tileProbe(&l.probe, l.direction);
        if (l.zResults[0] >= 2) return 3;
        l.direction +%= l.crossDiagonalOffset;
        l.offset = 0;
    } else {
        if (l.zResults[3] >= 2) return 3;
        l.crossOffset = 0;
        l.offset = 1;
    }
    setV(l.motion, "vz", l.crossOffset);
    setV(l.motion, "vx", l.cross);
    return moved(l);
}

/// The C's `slideZBack:`.
fn slideZBack(l: *Locals) c_int {
    l.direction -%= 2;
    l.crossDiagonal = -%l.diagonalOffset;
    l.cross = l.crossOffset;
    l.crossDiagonalOffset = -1;
    l.sideOffset = l.offset;
    l.cornerStep = 3;
    return slideZProbe(l);
}

/// The C's `slideZ:`: slide along a wall while moving mainly along z.
fn slideZ(l: *Locals) c_int {
    const a = l.actor;
    const pos = &a.unk0.position;
    if (word(a, 8) & 0x70000 != 0) return 3;
    signedSide(l, l.motion.vz < 0, 4, 0);
    l.direction -%= 1;
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.motion.vz +% l.offset);
    setV(&l.probe, "vx", @as(c_int, pos.vx) -% l.offset);
    l.zResults[0] = tileProbe(&l.probe, l.direction);
    l.direction +%= 2;
    l.opposite.vz = l.probe.vz;
    setV(&l.opposite, "vx", @as(c_int, pos.vx) +% l.offset);
    l.zResults[1] = tileProbe(&l.opposite, l.direction);
    if (l.zResults[0] == 0xFF and l.zResults[1] == 0xFF) return 3;
    l.crossOffset = @divTrunc(@as(c_int, l.motion.vz), 2);
    if (l.zResults[0] < 2) {
        if (l.zResults[1] < 2) {
            l.direction -%= 2;
            setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.motion.vz);
            setV(&l.probe, "vx", @as(c_int, pos.vx) -% l.offset);
            l.zResults[2] = tileProbe(&l.probe, l.direction);
            l.direction +%= 2;
            l.opposite.vz = l.probe.vz;
            setV(&l.opposite, "vx", @as(c_int, pos.vx) +% l.offset);
            l.zResults[3] = tileProbe(&l.opposite, l.direction);
            if (l.zResults[2] == 0xFF and l.zResults[3] == 0xFF) return 3;
            if (l.zResults[2] > 0) return slideZBack(l);
            if (l.zResults[3] <= 0 and (l.zResults[0] != 0 or l.zResults[1] != 1)) {
                if (l.zResults[0] == 1 and l.zResults[1] == 1) return slideZBack(l);
                if (l.probe.vy <= l.opposite.vy) return slideZBack(l);
            }
        }
        l.sideOffset = -%l.offset;
        l.crossDiagonal = l.diagonalOffset;
        l.cross = -%l.crossOffset;
        l.crossDiagonalOffset = 1;
        l.cornerStep = -3;
        return slideZProbe(l);
    }
    if (l.zResults[1] < 2) return slideZBack(l);
    return 3;
}

/// The facing test of a held-direction (0x4000) actor: true when it faces `expected`.
fn faces(a: *v.D_800F4538_t, expected: c_int) bool {
    return @as(c_int, a.unk17FE) == expected;
}

fn probeAxisX(l: *Locals) void {
    const pos = &l.actor.unk0.position;
    signedSide(l, l.motion.vx < 0, 6, 2);
    setV(&l.probe, "vx", @as(c_int, pos.vx) +% l.motion.vx +% l.offset);
    l.probe.vz = pos.vz;
    l.xResults[0] = tileProbe(&l.probe, l.direction);
    l.xProbe = l.probe;
    l.probeDirections[0] = l.direction;
    l.direction -%= 2;
    l.probeTiles[0] = v.g.D_800F49F0().*;
    setV(&l.probe, "vx", @as(c_int, l.probe.vx) -% l.offset);
    setV(&l.probe, "vz", @as(c_int, l.probe.vz) +% l.offset);
    l.xResults[1] = tileProbe(&l.probe, l.direction);
    l.direction +%= 4;
    setV(&l.probe, "vz", @as(c_int, l.probe.vz) -% l.offset *% 2);
    l.xResults[2] = tileProbe(&l.probe, l.direction);
    l.direction -%= 3;
    setV(&l.probe, "vx", @as(c_int, l.probe.vx) +% l.diagonalOffset);
    setV(&l.probe, "vz", @as(c_int, l.probe.vz) +% l.offset +% l.diagonalOffset);
    l.xResults[3] = tileProbe(&l.probe, l.direction);
    setV(&l.probe, "vz", @as(c_int, l.probe.vz) -% l.diagonalOffset *% 2);
    l.xResults[4] = tileProbe(&l.probe, l.direction +% 2);
}

fn probeAxisZ(l: *Locals) void {
    const pos = &l.actor.unk0.position;
    signedSide(l, l.motion.vz < 0, 4, 0);
    l.probe.vx = pos.vx;
    setV(&l.probe, "vz", @as(c_int, pos.vz) +% l.motion.vz +% l.offset);
    l.zResults[0] = tileProbe(&l.probe, l.direction);
    l.zProbe = l.probe;
    l.probeDirections[1] = l.direction;
    l.direction -%= 2;
    l.probeTiles[1] = v.g.D_800F49F0().*;
    setV(&l.probe, "vx", @as(c_int, l.probe.vx) -% l.offset);
    setV(&l.probe, "vz", @as(c_int, l.probe.vz) -% l.offset);
    l.zResults[1] = tileProbe(&l.probe, l.direction);
    l.direction +%= 4;
    setV(&l.probe, "vx", @as(c_int, l.probe.vx) +% l.offset *% 2);
    l.zResults[2] = tileProbe(&l.probe, l.direction);
    l.direction -%= 3;
    setV(&l.probe, "vx", @as(c_int, l.probe.vx) -% (l.offset +% l.diagonalOffset));
    setV(&l.probe, "vz", @as(c_int, l.probe.vz) +% l.diagonalOffset);
    l.zResults[3] = tileProbe(&l.probe, l.direction);
    setV(&l.probe, "vx", @as(c_int, l.probe.vx) +% l.diagonalOffset *% 2);
    l.zResults[4] = tileProbe(&l.probe, l.direction +% 2);
}

/// orig: func_800A76BC (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Collides `actor`'s step `motion` with the map: 0 free, 1 / 2 only x / z kept, 0-2 a wall slide (motion rewritten), 3 blocked; `probeOnly` only reports the tile ahead.
pub fn func_800A76BC(actor: ?*v.D_800F4538_t, motion: ?*v.SVECTOR, result: [*c]c_int, probeOnly: c_int) callconv(.c) c_int {
    const a = actor.?;
    const m = motion.?;
    const scratch: *v.D_1F8003BC_t = @ptrFromInt(0x1F8003BC);
    psx.put(&scratch.unk18, @as(*v.D_800F45E0_t, @ptrCast(a)));
    psx.setBits(a, "unk5AC_14", psx.getBits(a, "unk5AC_13"));
    psx.setBits(a, "unk5AC_13", 0);
    const radius: c_int = a.unk63C;
    var l = Locals{
        .actor = a,
        .motion = m,
        .result = result,
        .scratch = scratch,
        .radius = radius,
        .diagonal = @divTrunc(c.rsin(0x200) *% radius, 4096),
    };
    scratch.unk0 = -0xC0;
    if (word(a, 8) & 0x800000 != 0) {
        scratch.unk40 = -0x40;
    } else if (word(a, 0x5AC) & 8 != 0) {
        scratch.unk0 = -0x140;
        scratch.unk40 = -0x40;
    } else {
        scratch.unk40 = -0x100;
    }

    if (probeOnly != 0) {
        scratch.unk40 = -0x100;
        psx.setBits(a, "unk5AC_10", 0);
        psx.setBits(a, "unk5AC_12", 0);
        if (word(a, 0xC) & 0xF == 0) {
            l.probeTiles[0] = c.func_800A8D64(@ptrCast(&a.unk0.position), a.unk0.unk5D);
            if (l.probeTiles[0] == null) psx.setBits(a, "unk5AC_10", 1);
        }
        setV(&l.probe, "vx", @as(c_int, a.unk0.position.vx) +% m.vx);
        setV(&l.probe, "vz", @as(c_int, a.unk0.position.vz) +% m.vz);
        scratch.unk1C[0] = a.unk0.position.vy;
        scratch.unk0 = 0;
        l.xResults[0] = tileProbe(&l.probe, 0);
        v.g.D_800F4B08().* = l.probe;
        if (l.xResults[0] == 0xFF) {
            result[0] = 0;
            return 3;
        }
        v.g.D_800F49F0().* = l.probeTiles[0];
        result[0] = l.xResults[0];
        if (result[0] >= 2) result[0] = 1;
        return 0;
    }

    if (c.func_800A8FD4(@ptrCast(a), @ptrCast(m)) == 0) return 3;
    c.func_800A70DC(@ptrCast(a), l.diagonal);
    c.func_800A7524(@ptrCast(a), @ptrCast(m));
    psx.setBits(a, "unk5AC_10", 0);
    if (word(a, 0xC) & 0xF == 0) {
        l.probeTiles[0] = c.func_800A8D64(@ptrCast(&a.unk0.position), a.unk0.unk5D);
        psx.setBits(a, "unk5AC_10", @intFromBool(l.probeTiles[0] == null));
    }

    const Entry = enum { diagonal, axisX, axisZ };
    var entry: Entry = .diagonal;
    if (m.vx == 0) {
        l.xResults[0] = 0xFF;
        entry = .axisZ;
    } else if (m.vz == 0) {
        l.zResults[0] = 0xFF;
        entry = .axisX;
    }

    if (entry == .diagonal) {
        const pos = &a.unk0.position;
        l.direction = 6;
        if (m.vx < 0) {
            l.offset = -%radius;
            l.direction = 2;
            l.diagonalOffset = -%l.diagonal;
        } else {
            l.offset = radius;
            l.diagonalOffset = l.diagonal;
        }
        if (m.vz < 0) {
            l.crossOffset = -%radius;
            l.crossDiagonalOffset = -%l.diagonal;
            l.direction = if (l.direction == 2) 1 else 7;
        } else {
            l.crossOffset = radius;
            l.crossDiagonalOffset = l.diagonal;
            l.direction = if (l.direction == 2) 3 else 5;
        }
        setV(&l.probe, "vx", @as(c_int, pos.vx) +% m.vx +% l.diagonalOffset);
        setV(&l.probe, "vz", @as(c_int, pos.vz) +% m.vz +% l.crossDiagonalOffset);
        l.xResults[0] = tileProbe(&l.probe, l.direction);
        v.g.D_800F4B08().* = l.probe;
        l.probeTiles[0] = v.g.D_800F49F0().*;
        setV(&l.probe, "vx", @as(c_int, l.probe.vx) -% l.diagonalOffset *% 2);
        l.direction -%= 2;
        l.xResults[1] = tileProbe(&l.probe, l.direction);
        setV(&l.probe, "vx", @as(c_int, l.probe.vx) +% l.diagonalOffset *% 2);
        setV(&l.probe, "vz", @as(c_int, l.probe.vz) -% l.crossDiagonalOffset *% 2);
        l.direction +%= 4;
        l.xResults[2] = tileProbe(&l.probe, l.direction);
        setV(&l.probe, "vx", @as(c_int, l.probe.vx) -% l.diagonalOffset);
        setV(&l.probe, "vz", @as(c_int, l.probe.vz) +% l.crossDiagonalOffset +% l.crossOffset);
        l.direction -%= 3;
        l.xResults[3] = tileProbe(&l.probe, l.direction);
        setV(&l.probe, "vx", @as(c_int, l.probe.vx) +% l.offset);
        setV(&l.probe, "vz", @as(c_int, l.probe.vz) -% l.crossOffset);
        l.direction +%= 2;
        l.xResults[4] = tileProbe(&l.probe, l.direction);

        const steps = climb(v.g.D_800F4B08().vy, a);
        if (allWithin(&l.xResults, steps) and (l.xResults[0] < 4 or l.direction & 1 != 0)) {
            v.g.D_800F49F0().* = l.probeTiles[0];
            v.g.D_800F49F9().* = psx.conv(u8, (l.direction -% 1) & 7);
            result[0] = l.xResults[0];
            return 0;
        }
    }

    if (entry != .axisZ) {
        // From the diagonal the C tests motion->vx again (never 0 there).
        if (entry == .axisX or m.vx != 0) probeAxisX(&l) else l.xResults[0] = 0xFF;
    }
    if (entry == .axisZ or m.vz != 0) probeAxisZ(&l) else l.zResults[0] = 0xFF;

    const x: c_int = m.vx;
    const z: c_int = m.vz;

    if (abs(x) == abs(z) or abs(x) >= abs(z)) {
        // xProbe is unset when x is 0 (then xResults[0] is 0xFF and the steps are unused).
        const steps = if (l.xResults[0] != 0xFF) climb(l.xProbe.vy, a) else 1;
        if (allWithin(&l.xResults, steps)) {
            if (word(a, 0x5AC) & 0x4000 != 0) {
                if (faces(a, if (x < 0) 6 else 2)) return slideX(&l);
            }
            return acceptX(&l);
        }
        if (allBelow2(&l.zResults)) {
            if (word(a, 0x5AC) & 0x4000 != 0) {
                if (m.vz < 0) {
                    if (a.unk17FE == 4) return slideX(&l);
                } else if (a.unk17FE == 0) return slideX(&l);
            }
            return acceptZ(&l);
        }
        return slideX(&l);
    }

    const steps = if (l.zResults[0] != 0xFF) climb(l.zProbe.vy, a) else 1;
    if (allWithin(&l.zResults, steps)) {
        if (word(a, 0x5AC) & 0x4000 != 0) {
            if (z < 0) {
                if (a.unk17FE == 4) return slideZ(&l);
            } else if (a.unk17FE == 0) return slideZ(&l);
        }
        return acceptZ(&l);
    }
    if (allBelow2(&l.xResults)) {
        if (word(a, 0x5AC) & 0x4000 != 0) {
            if (faces(a, if (m.vx < 0) 6 else 2)) return slideZ(&l);
        }
        return acceptX(&l);
    }
    return slideZ(&l);
}
