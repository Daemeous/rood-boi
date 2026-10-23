//! Moves an enemy actor to a tile, either keeping its facing or setting the tile's
//! position and a target angle (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A97EC (src/BATTLE/BATTLE.PRG/40564.c)
/// Places enemy slot `arg0` on the tile described by `arg1`, turning it to face `arg1`'s angle when `arg2` and `arg3` are 0, else setting its move state.
pub fn func_800A97EC(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int, arg3: c_int) callconv(.c) void {
    // ROOD_PC: an empty enemy slot is skipped (the PS1 writes low RAM through NULL here).
    const actor: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).* orelse return;

    if (arg2 == 0 and arg3 == 0) {
        c.func_800AA290(arg0, @ptrCast(arg1), -1, 0);

        var angle: c_int = @as(c_int, psx.getBits(arg1.?, "unk0_24"));
        angle <<= 4;
        angle = @rem(angle, 4096);
        angle -%= @as(c_int, actor.unk0.facing);

        c.func_800AA984(arg0, psx.conv(i16, angle), 0);

        psx.setBits(actor, "unk5AC_0", 0);
        actor.unk0.currentTileX = psx.getBits(arg1.?, "unk0_0");
        actor.unk0.unk5D = psx.getBits(arg1.?, "unk0_8");
        actor.unk0.currentTileZ = psx.getBits(arg1.?, "unk0_16");
    } else {
        if (arg3 == -1) {
            actor.unk5C6 = actor.unk5C0;
        } else {
            actor.unk5C6 = psx.conv(u16, arg3);
        }

        if (arg2 == -1) {
            actor.unk5C4 = actor.unk5C2;
        } else {
            actor.unk5C4 = psx.conv(u16, arg2);
        }

        c.func_800AA490(arg0, @ptrCast(arg1), -1, @as(c_int, actor.unk5C6));

        actor.unk5EC.vx = psx.conv(i16, @as(c_int, psx.getBits(arg1.?, "unk0_0")) *% 128 +% 64);
        actor.unk5EC.vz = psx.conv(i16, @as(c_int, psx.getBits(arg1.?, "unk0_16")) *% 128 +% 64);
        actor.unk5EC.vy = psx.conv(i16, c.func_800A6EE8(psx.cptr(c.SVECTOR, &actor.unk5EC), 0, 0, 1));

        if (psx.getBits(arg1.?, "unk0_24") == 0xFF) {
            actor.unk5EC.pad = -0x8000;
        } else {
            actor.unk5EC.pad = psx.conv(i16, @as(c_int, psx.getBits(arg1.?, "unk0_24")) *% 16);
        }

        psx.setBits(actor, "unk5AC_0", 1);
        psx.setBits(actor, "unk5AC_2", 0);
    }
}
