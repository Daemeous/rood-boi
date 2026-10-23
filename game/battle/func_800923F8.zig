//! Steps one swinging rotation channel and writes its angle into the room geometry (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_800923F8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Steps a swinging angle channel, writes its angle into the room geometry, and returns 1 while it still swings.
pub fn func_800923F8(arg0: ?*v.D_800F1DD8_t2) callconv(.c) c_int {
    const base: ?*i32 = psx.get(v.g.vs_battle_roomData().geometrySection);
    const s = arg0.?;
    var ret: c_int = 1;

    if (s.unk4 != 0) {
        s.unk4 = 0;
        s.unk14 = s.unk1C;
        s.unk8 = s.unk20;
    }

    s.unk14 +%= s.unk8;
    s.unk8 +%= s.unkC *% 8;
    var delta: i32 = (s.unk14 -% s.unk18) & 0xFFF;

    if (delta > 4096 / 2) {
        delta = 4096 -% delta;
    }

    const abs8: i32 = if (s.unk8 >= 0) s.unk8 else -%s.unk8;
    if (delta < abs8) {
        // The shift amount is masked to 0..31, as the MIPS and x86 shifts do.
        const sh: u5 = @truncate(@as(u32, @bitCast(s.unk10)));
        s.unk8 = -%(s.unk8 >> sh);

        const abs8b: i32 = if (s.unk8 >= 0) s.unk8 else -%s.unk8;
        if (abs8b < 0x21) {
            s.unk14 = s.unk18;
            ret = 0;
        }
    }

    const off: isize = s.unk0;
    if (s.unk7 == 1) {
        psx.byteAt(i32, base.?, off + 20).* = s.unk14;
        psx.byteAt(i32, base.?, off).* |= 0x6000;
    } else if (s.unk7 == 2) {
        psx.byteAt(i32, base.?, off + 16).* = s.unk14;
    } else {
        psx.byteAt(i32, base.?, off + 24).* = s.unk14;
    }

    if (ret != 0) {
        return 1;
    }

    delta = s.unk1C;
    s.unk20 = -%s.unk20;
    s.unk8 = s.unk20;
    s.unk1C = s.unk18;
    s.unk18 = delta;
    s.unkC = -%s.unkC;
    return 0;
}
