//! Applies the battle ability effects queued for one actor and runs the matching
//! reaction, or the arg1 hook for slots 0x80 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_applyActionTargetEffects (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Applies the ability effects aimed at actor `arg0` and runs its reaction; slots of 16 and above run the arg1 hook.
pub fn vs_battle_applyActionTargetEffects(arg0: c_int, arg1: ?*v.func_8006CE70_t) callconv(.c) void {
    if (arg0 < 16) {
        var applied = false;
        var effect: c_int = 6;
        const base = v.g.vs_battle_actionState().*.?;
        const temp_s0: *v.D_800F19CC_t2 = &base.queue[base.unk0 & 3];

        var i: c_int = 0;
        while (i < @as(c_int, temp_s0.targetCount)) : (i +%= 1) {
            const entry = psx.at(&temp_s0.targets, i);
            if (@as(c_int, entry.target.targetActor) == arg0 and entry.unk40 == 0) {
                switch (@as(c_int, entry.target.unk3)) {
                    0, 1 => {
                        effect = 0;
                    },
                    5 => {
                        effect = 5;
                    },
                    3, 4 => {
                        const word = @as(*align(1) u32, @ptrCast(psx.at(v.g.actions(), temp_s0.actionIndex))).*;
                        const flag = v.g.D_800E8194()[(word >> 20) & 15];
                        if (flag == 0 and effect == 6) {
                            effect = 4;
                        }
                    },
                    else => {},
                }

                c._applyBattleAbilityEffect(@ptrCast(entry));
                applied = true;
            }
        }

        switch (effect) {
            0 => {
                c.func_8009EA14(arg0, psx.cptr(c.SVECTOR, &base.rangeOrigin));
            },
            5 => {
                if (arg0 == 0) {
                    c.func_8009E070(0, null, 6);
                }
            },
            4 => {
                c.func_8009DDF4(arg0, psx.cptr(c.SVECTOR, &base.rangeOrigin));
            },
            else => {},
        }
        if (applied) {
            effect = 0;
        }
    } else {
        switch (arg0 & 0x1F0) {
            0x10, 0x20 => {},
            0x80 => {
                c.vs_battle_queueWorldLockedLabel(@ptrCast(arg1));
            },
            else => {},
        }
    }
}
