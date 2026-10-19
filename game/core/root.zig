//! game_core.o: linked into every build (pc/docs/Ownership.md). ownership.c
//! logs the ABI version at start, so every target exercises the toolchain.
const common = @import("common");
pub const panic = common.Panic;

/// Bumped when the routing ABI between C and owned code changes.
export const rood_game_abi: u32 = 1;
