# Modern Menus — Phase 5: Abilities Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Abilities tab becomes native: sub-tabs Chain, Defense and Break Arts (L2/R2). Chain/Defense show the three button slots (Circle, Triangle, Square) and the learned abilities with RISK cost and description; assigning works exactly like the original menu. Break Arts lists the ten weapon categories with learned arts (HP cost, description), locked arts as "???" with kills to go, and casts an art natively through the game's own executor with the original's checks.

**Architecture:** A battle-side `pc/src/ui/abilities_bridge.c` fills a plain-C `pc_ui_abilities` snapshot (names decoded from `vs_main_actions[].name`, descriptions from MENU2/MENU1's generated string assets compiled in as static copies, as `ashley_bridge.c` does for MENU4) and executes requests: `ABIL_MAP` (MENU2 `_mapAbility` semantics) and `ART_CAST` (MENU1's hand-off fields, then the shell closes through the normal path). The shell gets an Abilities screen; the draw layer an `ui_abilities_draw.cpp`.

**Tech Stack:** as earlier phases.

**Ground rules:** as earlier phases (branch `claude/musing-haslett-8e55eb`, merge main first, `src/` only under ROOD_PC + commit-check, three builds + decl check, `run.py -j 2` one at a time, never build in the main checkout, no personal paths / emails / other-project names / game data in commits, scan the branch diff before the merge request). Don't touch files other agents own (button prompts, ui_text, atmosphere/haze).

**Facts (research brief):**
- Learned lists: `vs_battle_chainAbilityOffsets[14]` / `vs_battle_defenseAbilityOffsets[14]` (BATTLE data, extern in `5BF94.h:710-711`) → action ids; keep those with `vs_main_actions[id].unlocked`. Description of entry i = MENU2 `battleAbilities.vsString` index `chainAbilityEffect0 + i` / `defenseAbilityEffect0 + i` (position in the offsets table, not the action id). RISK cost = `vs_main_actions[id].cost` (usable in battle when cost ≤ 100 − risk).
- Mapping: `vs_main_settings.mappedChainAbilities[0..2]` / `mappedDefenseAbilities[0..2]` (main.h:145-146), slot 0 Circle, 1 Triangle, 2 Square, value = action id, 0 = none. MENU2 `_mapAbility` (64.c:166): if that button already holds the ability → clear it (leave sfx); else play `vs_main_playSfxDefault(0x7E, VS_SFX_SET)`, clear the ability from slots 0..2, write it to the button (moves, no swap back). No other side effects (battle reads the mapping live).
- "Next ability": `vs_main_battleAbilitiesPointsRequirements[kills.battleAbilitiesUnlocked] - kills.total` (≥ 0), none when `battleAbilitiesUnlocked == 22` (64.c:612).
- Break Arts: 4 per category, `actionId = 184 + (cat-1)*4 + i` (cat 1..10, 10 = bare hands); names `vs_main_actions[id].name` (decode up to 0xE7 with `pc_ui_text_decode`); descriptions MENU1 `strings.vsString` index `daggerArt1 + (cat-1)*4 + i`; category name/desc/"no arts" `INDEX_dagger + (cat-1)*3 (+1, +2)`. HP cost = `vs_main_actions[id].cost` (usable when HP > cost). Kills: `c = cat % 10`; learned count `vs_main_artsStatus.artsLearned[c]`; kills `kills.weaponCategories[c]`; locked art k: `max(0, vs_main_artsPointsRequirements[c][k] - kills[c])` (0 = learned after the next battle).
- Casting (MENU1 30.c:403-411, 452-460): allowed when `cat == vs_battle_characterState->equippedWeaponCategory`, `vs_battle_getActionFlags(0, id) == 0` (bits: 1 busy, 2 can't afford, 4 locked/sheathed/status), `!(D_800F4EA0 & 0x15F)`. Hand-off: `D_800F4E98.executeAbility.s32 = id; vs_battle_menuState.executeAbilityType = 5; D_800F4E98.unk2 = 6; vs_battle_menuState.returnState = 0x42;` then close the menu (the shell's −1 path: 0x1F → `_quitDelay` → 0x7F → `func_800735F8` executes). Target cancel returns to `returnState` 0x42 = the original Break Arts list once; Cross there returns to the shell.
- Gating: menuFlags bit1 Break Arts, bit2 Battle Abilities (shell `commands().enabled`); Battle Abilities never greyed; Break Arts greyed by `D_800F4EA0 & 0x15F` (reasons already in `ui_shell.cpp row_reason`).
- Assets: `build/assets/MENU/MENU2.PRG/battleAbilities.h/.vsString`, `battleAbilitiesMenu.h/.vsString` (category names), `build/assets/MENU/MENU1.PRG/strings.h/.vsString`.

## Tasks

### Task 1: Snapshot + shell Abilities screen (pure) + probe
- [ ] `ui_model.h`: `pc_ui_abilities` { valid; chain/defense: count, entries[14] {id, name[32], help[200], cost, button (−1/0/1/2)}, slots[3] (entry index or −1); next_ability_kills (−1 none); equipped_category; risk, hp; categories[10] {name[24], help[160], learned, kills, next_kills (−1 all learned), arts[4] {id, name[32], help[200], cost, state (0 locked, 1 pending (0 to go), 2 learned), kills_to_go, usable, reason[64]}} }.
- [ ] Shell: Abilities tab = native screen with sub-tabs Chain / Defense / Break Arts (L2/R2; only those unlocked by menuFlags; Break Arts hidden when bit1 clear). Chain/Defense: focus moves over the three slots (Left/Right or Up/Down) and the learned list; Circle on a slot → pick mode (list) → Circle on an ability = `ABIL_MAP {type, button, id}`; Circle/Triangle/Square pressed while a list ability is focused = map to that button directly (the original's direct-press style). Break Arts: categories list (equipped first, marked) → arts; Circle on a usable learned art = `ART_CAST {id}` (the shell closes after the bridge accepts), greyed with reason otherwise; locked = "???" + kills. JSON for tests. Probe cases.

### Task 2: Abilities bridge (`abilities_bridge.c`)
- [ ] Fill (every tick while the Abilities tab shows; cheap), requests: `ABIL_MAP` exactly as `_mapAbility` (incl. sounds); `ART_CAST` with the exact checks + hand-off fields, then the shell returns −1 through the normal deliver path (play the select sfx, not the leave sfx). Runtime sanity via RPC (card copy; unlock a break art in RAM if the save has none, as `ui_menus_smoke` does).

### Task 3: Drawing (`ui_abilities_draw.cpp`)
- [ ] Per the prototype's Abilities tab: sub-tabs with L2/R2 prompts; three slot cards with the prompt icon (Circle/Triangle/Square), ability name, RISK cost; learned list with the button badge where mapped, RISK cost; right card = description + RISK + "Press on the hit" (chain) / "Press as you are hit" (defense); Break Arts: category rail (name, learned n/4, equipped mark, next art in N kills), arts list (name or "???", HP cost or kills to go, pending "Learned after the next battle"), card with description and why it can't be used (sheathed, HP too low, other weapon type). 4:3 + 16:9; mouse.

### Task 4: Test `ui_abilities_smoke.py`
- [ ] Mapping parity with the original MENU2 (same presses through the original menu with `ui_menus` off vs the shell → identical `vs_main_settings.mapped*` bytes; toggle-off; move between buttons); Break Arts: unlocked art listed with cost; locked "???" with kills = requirement − kills; cast: with the weapon drawn and HP enough → after Circle the menu closes and the game enters targeting (`D_800F196C` / execute state) and the art executes on confirm (HP decreases by the cost); greyed when sheathed (reason) / wrong category / HP too low; target cancel → original Break Arts list → Cross → shell. Both archs.

### Task 5: Docs, review, merge request
- [ ] README + Modern_Menus.md; code review; fixes; merge-request checks; merge request; tell the user.
