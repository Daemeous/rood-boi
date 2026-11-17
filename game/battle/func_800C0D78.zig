//! Builds the scratchpad sine table and draws the two shapes of the shape table
//! (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const gte = common.gte;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: func_800C0D78 (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Fills the scratchpad sine table, then projects each shape of the shape table and draws it.
pub fn func_800C0D78() callconv(.c) void {
    var shape: *v.func_800C1564_t = pad.at(v.func_800C1564_t, 0x378);
    var color: c_int = 0x10401;

    const d = v.g.vs_battle_targetSphere().* orelse return;

    const sine: [*]i16 = pad.many(i16, 0x3B0);
    var k: usize = 0;
    while (k < 40) : (k += 1) {
        sine[k] = psx.conv(i16, c.rsin(@as(c_int, @intCast(k)) << 7));
    }

    var i: usize = 0;
    while (i < 2) : ({
        color = 0x10104;
        shape = @ptrFromInt(@intFromPtr(shape) +% @sizeOf(v.func_800C1564_t));
        i += 1;
    }) {
        shape.* = d.unk0[i];
        c.vs_render_loadFrameViewMatrix();
        const frame = gte.frame();
        @as(*align(1) c_uint, @ptrCast(&d.unk20)).* = gte.project(&frame, &shape.unk8).otz; // gte_stszotz: a whole word, as the macro stores it

        const typ: c_int = switch (@as(c_int, shape.unk0)) {
            1 => 0,
            2 => blk: {
                shape.unk4.values[1] = psx.conv(u8, @as(c_int, shape.unk4.values[1]) << 1);
                const mat: *align(1) v.MATRIX = @ptrCast(c.func_800C085C(&shape.unk4.values[0], @as(c_int, shape.unk2)));
                const vec: [*]i16 = @ptrCast(&shape.unk8);
                var j: usize = 0;
                while (j < 3) : (j += 1) {
                    vec[j] = psx.conv(i16, @as(c_int, vec[j]) +% (@as(c_int, mat.m[j][1]) >> 1));
                }
                break :blk 1;
            },
            3 => 1,
            4 => blk: {
                shape.unk4.values[3] = psx.conv(u8, @as(c_int, shape.unk4.values[3]) +% 0x80);
                const mat: *align(1) v.MATRIX = @ptrCast(c.func_800C085C(&shape.unk4.values[0], @as(c_int, shape.unk2)));
                const vec: [*]i16 = @ptrCast(&shape.unk8);
                var j: usize = 0;
                while (j < 3) : (j += 1) {
                    vec[j] = psx.conv(i16, @as(c_int, vec[j]) +% @as(c_int, mat.m[j][1]));
                }
                break :blk 2;
            },
            5 => 2,
            6 => 3,
            7 => 4,
            else => continue,
        };

        c.func_800C253C(typ);
        c.vs_battle_drawSphereVolumeWireframe(@ptrCast(shape), color);
    }
    c.func_800C20B4();
}
