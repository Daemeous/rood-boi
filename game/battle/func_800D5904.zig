//! An effect command: plays or stops a sound effect at its frame (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5904 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// At the command's frame: kind 2 stops the P-file's sound (func_80046578), else plays sound (command >> 9) & 0x1FF of bank 0x7E or 0xF00000, panned at an entry's translation when bits 20..21 say so; then the next command; else 1.
pub fn func_800D5904(script_arg: ?*v.D_800F53B8_t, cmd_arg: ?*v.func_800D5780_t) callconv(.c) c_int {
    const script = script_arg.?;
    const cmd = cmd_arg.?;
    var position: v.SVECTOR = undefined;
    const commands: [*]u32 = @ptrCast(psx.get(cmd.unk0).?);
    const command: c_uint = psx.at(commands, cmd.unk6).*;
    if ((command & 0x1FF) == psx.conv(c_uint, @as(c_int, psx.get(script.unkD1C.unk30).?.unk2))) {
        const kind: c_int = @intCast((command >> 18) & 3);
        if (kind == 2) {
            const pfile = v.g.D_800F569C().*.?;
            if (pfile.unkCC != 0) {
                c.func_80046578(pfile.unkCC);
            }
        } else {
            const bank: c_int = if (kind == 0) 0x7E else 0xF00000;
            if (((command >> 20) & 3) == 0) {
                c.vs_main_playSfxDefault(bank, @intCast((command >> 9) & 0x1FF));
            } else {
                const entry = psx.at(&script.unk1C, (psx.at(commands, cmd.unk6).* >> 22) & 63); // unk1C[16]: up to 63, unchecked
                position.vx = psx.conv(i16, entry.unk38.t[0]);
                position.vy = psx.conv(i16, entry.unk38.t[1]);
                position.vz = psx.conv(i16, entry.unk38.t[2]);
                c.vs_main_panSfx(bank, @intCast((psx.at(commands, cmd.unk6).* >> 9) & 0x1FF), @ptrCast(&position));
            }
        }
        return c.func_800D5780(@ptrCast(cmd));
    }
    return 1;
}
