//! Queues a move packet that copies a texture block of one part of a battle actor to the screen (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// Reads the packet cursor from scratchpad slot 0 (vs_battlePacketBegin).
fn packetBegin() *v.DR_MOVE {
    const p: u32 = @as(*const u32, @ptrFromInt(0x1F800000)).*;
    return @ptrFromInt(@as(usize, p));
}

/// Stores the packet cursor past `p` into scratchpad slot 0 (vs_battlePacketEnd).
fn packetEnd(p: *v.DR_MOVE) void {
    @as(*u32, @ptrFromInt(0x1F800000)).* = psx.addr(psx.at(p, 1));
}

/// orig: func_800AB358 (src/BATTLE/BATTLE.PRG/40564.c)
/// Queues a move packet copying part `arg[1]` of actor `index` from texture page `arg[2..3]` to the screen.
pub fn func_800AB358(index: c_int, arg: [*c]u8) callconv(.c) void {
    // The actor pointer is only dereferenced inside the branch, as in the C.
    const actorPtr: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), index).*;

    if (v.g.D_800F2450()[v.g.D_800E8F2C().*] != 0) {
        const actorStruct = actorPtr.?;
        const actor: [*]u8 = @ptrCast(actorStruct);

        var srcX: c_int = arg[2];
        var srcY: c_int = arg[3];
        srcY +%= @as(c_int, actor[@as(usize, v.g.D_800E8F2C().*) + 0x183E]);

        const partOff: usize = 0x5F4 + @as(usize, arg[1]) * 4;
        var dstX: c_int = actor[partOff];
        var width: c_int = actor[partOff + 2];
        const height: c_int = actor[partOff + 3];

        if (psx.getBits(actorStruct, "unk5AC_27") == 0) {
            srcX = @divTrunc(srcX, 4);
            width = @divTrunc(width, 4);
            dstX = @divTrunc(dstX, 4);
        } else {
            srcX = @divTrunc(srcX, 2);
            width = @divTrunc(width, 2);
            dstX = @divTrunc(dstX, 2);
        }

        const packet = packetBegin();
        const slot: usize = actorStruct.unk5BB;

        const table = v.g.D_800E8F30();
        const x: c_int = @as(*align(1) const i16, @ptrCast(table + slot * 4)).*;
        const y: c_int = @as(*align(1) const i16, @ptrCast(table + slot * 4 + 2)).*;
        const row: c_int = v.g.D_800E8F90()[slot];
        const py: c_int = actor[partOff + 1];
        const page: c_int = v.g.D_800F2450()[v.g.D_800E8F2C().*];

        var rect: v.RECT = undefined;
        rect.y = psx.conv(i16, srcY +% 256);
        rect.w = psx.conv(i16, width);
        rect.h = psx.conv(i16, height);
        rect.x = psx.conv(i16, ((page & 15) << 6) +% srcX);

        c.SetDrawMove(
            psx.cptr(c.DR_MOVE, packet),
            psx.cptr(c.RECT, &rect),
            dstX +% (x << 6),
            (y << 8) +% (row << 6) +% py,
        );
        c.func_8007A824(psx.cptr(c.DR_MOVE, packet));
        packetEnd(packet);
    }
}
