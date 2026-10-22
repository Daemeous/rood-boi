//! Decodes the packed animation stream into an SVECTOR's deltas (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800AFDE8 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Advances an SVECTOR's animation stream over the scratchpad's frame range, applying each frame's deltas.
pub fn func_800AFDE8(offset: c_int, value: ?*v.SVECTOR, rotation: c_int) callconv(.c) void {
    const mem = struct {
        fn byte(a: u32) u8 {
            return @as(*align(1) const u8, @ptrFromInt(@as(usize, a))).*;
        }
        fn s16(a: u32) c_int {
            return (@as(c_int, @as(i8, @bitCast(byte(a)))) << 8) | @as(c_int, byte(a +% 1));
        }
        fn word(a: u32) u32 {
            return @as(*align(1) const u32, @ptrFromInt(@as(usize, a))).*;
        }
    };

    const scratch: *align(1) c.animationFrameScratch = @ptrFromInt(0x1F8003EC);
    const val = value.?;
    const delta = psx.byteAt(v.SVECTOR, val, 0x2A0);

    var data: u32 = psx.conv(u32, psx.conv(u16, val.pad));
    var nextFrame: c_int = @as(c_int, psx.conv(u16, delta.pad));
    var frame: c_int = psx.conv(c_int, scratch.first);
    var flags: c_int = 0;
    var count: c_int = 0;
    var packedBits: c_int = 0;

    data +%= psx.conv(u32, offset) +% mem.word(psx.rawOf(scratch.data));

    while (frame <= psx.conv(c_int, scratch.last)) {
        while (nextFrame < frame) {
            flags = @as(c_int, mem.byte(data));
            if (flags == 0) return;
            if ((flags & 0xE0) != 0) {
                count = flags & 0x1F;
                if (count == 0x1F) {
                    nextFrame +%= 0x20;
                    nextFrame +%= @as(c_int, mem.byte(data +% 1));
                    data +%= 2;
                } else {
                    nextFrame +%= count +% 1;
                    data +%= 1;
                }
            } else {
                count = flags & 3;
                if (count == 3) {
                    nextFrame +%= 4;
                    nextFrame +%= @as(c_int, mem.byte(data +% 1));
                    data +%= 2;
                } else {
                    nextFrame +%= count +% 1;
                    data +%= 1;
                }
                flags <<= 3;
                count = mem.s16(data);
                data +%= 2;
                packedBits = count;
                count >>= 3;
                vz: {
                    if ((packedBits & 4) != 0) {
                        delta.vx = psx.conv(i16, count);
                        flags &= 0x60;
                        if ((packedBits & 3) != 0) {
                            count = mem.s16(data);
                            data +%= 2;
                        }
                    }
                    if ((packedBits & 2) != 0) {
                        delta.vy = psx.conv(i16, count);
                        flags &= 0xA0;
                        if ((packedBits & 1) != 0) {
                            count = mem.s16(data);
                            data +%= 2;
                        } else break :vz;
                    }
                    if ((packedBits & 1) != 0) {
                        delta.vz = psx.conv(i16, count);
                        flags &= 0xC0;
                    }
                }
            }
            // bytes:
            if ((flags & 0x80) != 0) {
                delta.vx = @as(i8, @bitCast(mem.byte(data)));
                data +%= 1;
            }
            if ((flags & 0x40) != 0) {
                delta.vy = @as(i8, @bitCast(mem.byte(data)));
                data +%= 1;
            }
            if ((flags & 0x20) != 0) {
                delta.vz = @as(i8, @bitCast(mem.byte(data)));
                data +%= 1;
            }
        }

        val.vx = psx.conv(i16, @as(c_int, val.vx) +% @as(c_int, delta.vx));
        val.vy = psx.conv(i16, @as(c_int, val.vy) +% @as(c_int, delta.vy));
        val.vz = psx.conv(i16, @as(c_int, val.vz) +% @as(c_int, delta.vz));
        if (rotation != 0) {
            // *(int*)value &= 0x1FFF1FFF: vx and vy; then vz.
            val.vx &= 0x1FFF;
            val.vy &= 0x1FFF;
            val.vz &= 0x1FFF;
        }
        frame +%= 1;
    }

    data -%= psx.conv(u32, offset) +% mem.word(psx.rawOf(scratch.data));
    val.pad = psx.conv(i16, data);
    delta.pad = psx.conv(i16, nextFrame);
}
