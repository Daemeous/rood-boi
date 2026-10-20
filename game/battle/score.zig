//! Score data a battle updates: kill counts, the kill streak and the streak
//! score (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _updateEnemyKills (src/BATTLE/BATTLE.PRG/146C.c)
/// Counts a defeated enemy: its class's kills, the kill streak, the streak score and its kill flag.
pub fn updateEnemyKills(enemy: ?*v.vs_battle_actor) callconv(.c) void {
    const score = v.g.scoreData();
    const subType: c_uint = enemy.?.subType;

    if (psx.get(enemy.?.stats) != null and @as(c_int, enemy.?.allegiance) == 4) {
        const actor = psx.get(enemy.?.stats).?;

        // enemyKills has 6 entries and enemyClass 3 bits: the C does not check 6 and 7.
        const kills = psx.at(&score.enemyKills, @as(c_int, psx.getBits(actor, "enemyClass")));
        if (@as(c_int, kills.*) < 65535) {
            kills.* +%= 1;
        }

        if (@as(c_int, score.killStreakEnemyClass) != @as(c_int, psx.getBits(psx.get(enemy.?.stats).?, "enemyClass"))) {
            score.killStreakEnemyClass = psx.conv(u8, @as(c_int, psx.getBits(psx.get(enemy.?.stats).?, "enemyClass")));
            score.killStreakEnemyCount = 0;
        } else if (@as(c_int, score.killStreakEnemyCount) < 100) {
            score.killStreakEnemyCount +%= 1;
        }

        const maxHP: c_int = actor.maxHP;
        score.streakScore +%= psx.conv(c_uint, maxHP +%
            @divTrunc(maxHP *% score.killStreakEnemyCount, 2) +%
            @divTrunc(maxHP *% score.weaponKillStreak, 2) +%
            @divTrunc(maxHP *% score.chainStreak, 2));

        if (score.streakScore > 999999999) {
            score.streakScore = 999999999;
        }
    }

    if (subType < 256) {
        // subType < 256: the index is below 8 of 26.
        score.enemyKillFlags[subType / 32] |= @bitCast(@as(c_uint, 1) << @intCast(subType % 32));
    }
}
