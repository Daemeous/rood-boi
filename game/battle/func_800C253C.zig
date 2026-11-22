//! Builds the menu circle's vertices and advances its shape state (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const psx = @import("common").psx;
const pad = @import("common").pad;
const v = @import("views_58578");
const c = @import("c_58578");

/// The battle menu's data block D_800EA130 (1264 bytes): the target sphere's tables are views into it, at the offsets the old labels had.
fn menuBlock() [*]u8 {
    return @ptrCast(v.g.D_800EA130());
}
fn sphereBasis() [*]i16 {
    return @ptrCast(@alignCast(menuBlock() + 0x104)); // D_800EA234: 72 shorts
}
fn sphereVertices() [*]i16 {
    return @ptrCast(@alignCast(menuBlock() + 0x194)); // D_800EA2C4: 186 shorts, negated in place
}
fn sphereStarts() [*]u8 {
    return menuBlock() + 0x308; // vs_battle_sphereShapeCommandStarts: 52 bytes
}
fn sphereCommands() [*]u16 {
    return @ptrCast(@alignCast(menuBlock() + 0x33C)); // vs_battle_sphereWireframeCommands: 218 words
}

/// orig: func_800C253C (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Lays out the menu circle's vertices for `type_` from its command list and records the vertex count.
pub fn func_800C253C(type_: c_int) callconv(.c) void {
    const basis: [*]i16 = pad.many(i16, 0x398);
    var index: c_int = 0;
    var i: c_int = 0;
    var j: c_int = 0;
    const ps = v.g.vs_battle_targetSphere();

    var shape: c_int = @as(c_int, ps.*.?.unk2A);
    if (shape != 0) {
        ps.*.?.unk2A = psx.conv(i16, shape -% 1);
    }
    const vertices = psx.byteAt(v.menuCircleVertex, ps.*.?, @offsetOf(v.D_800EB9B8_t, "points"));

    if (psx.conv(c_uint, type_) < 3) {
        if (type_ == 0) {
            ps.*.?.unk24 = psx.conv(i16, @as(c_int, ps.*.?.unk24) +% (shape +% 1) *% 3);
            ps.*.?.unk28 = psx.conv(i16, @as(c_int, ps.*.?.unk28) +% (shape +% 1) *% 11);
        }
        ps.*.?.unk26 = psx.conv(i16, @as(c_int, ps.*.?.unk26) +% (shape *% 2 +% 1) *% 7);
        shape +%= type_ *% 16;
    } else {
        shape = type_ +% 0x2D;
    }

    var command: c_int = @as(c_int, psx.at(sphereStarts(), shape).*);
    var count: c_int = @as(c_int, psx.at(sphereStarts(), shape +% 1).*) -% command;
    var cmd_pos: c_int = command;

    while (count > 0) : (count -%= 1) {
        command = @as(c_int, psx.at(sphereCommands(), cmd_pos).*);
        cmd_pos +%= 1;
        switch ((command >> 14) & 3) {
            0 => {
                var s: c_int = (command << 2) & 0x7C;
                const table = sphereBasis();
                i = 0;
                while (i < 3) : (i += 1) {
                    psx.at(basis, i).* = psx.at(table, s).*;
                    s +%= 1;
                }
                i = 4;
                while (i < 11) : (i += 1) {
                    psx.at(basis, i).* = 0;
                }
                j = (command >> 3) & 0x1C;
                i = @as(c_int, psx.at(table, s).*);
                const j5: u5 = @intCast(j);
                psx.at(basis, (@as(c_int, 0x546465) >> j5) & 0xF).* = psx.conv(i16, i);
                psx.at(basis, (@as(c_int, 0x8A998A) >> j5) & 0xF).* = psx.conv(i16, i);
                i = (command >> 9) & 0x1C;
                if (i == 0) i = 0x20;
                index = c.func_800C0758((command >> 6) & 0x1C, i, index);
            },
            1 => {
                index = c.func_800C2368(command & 0x3FF, ((command >> 10) & 7) + 1, index);
            },
            2 => {
                var src: c_int = command & 0x3FF;
                const table = sphereVertices();
                command = ((command >> 10) & 7) + 2;
                j = 0;
                while (j < 2) : (j += 1) {
                    i = command;
                    while (i > 0) : (i -= 1) {
                        const vert = psx.at(vertices, index);
                        vert.xyz[0] = psx.at(table, src).*;
                        psx.at(table, src).* = psx.conv(i16, -%@as(c_int, vert.xyz[0]));
                        vert.xyz[1] = psx.at(table, src + 1).*;
                        vert.xyz[2] = psx.at(table, src + 2).*;
                        psx.at(table, src + 2).* = psx.conv(i16, -%@as(c_int, vert.xyz[2]));
                        vert.flags = 1;
                        index +%= 1;
                        src +%= 3;
                    }
                    psx.at(vertices, index -% command).flags = 0;
                    src -%= command *% 3;
                }
            },
            else => {},
        }
    }

    ps.*.?.unk32 = psx.conv(i16, index);
}
