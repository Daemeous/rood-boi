# Enemy difficulty (native PC)

In F1 > Graphics > Gameplay, type whole percentages into **Base increase (%)**
and **Additional increase per completion (%)**. Both default to zero. Use
**Save settings** to keep them in the launcher's settings file.

The increase is `base + completed games * per-completion`, without compounding.
For example, 30 and 30 give +30% on the first playthrough and +90% on the third.
Inputs accept 0–10000%. The game records at most 99 completions.

Ashley (slot 0) and Merlose (model 8, including her dynamic companion slot) are
excluded. Other characters use the adjustment. HP, MP, STR, INT and AGL keep
the game's 999 limit. Zero MP stays zero. Each limb keeps its original fraction
of the character's maximum HP: its new maximum is
`round(original limb HP * capped new maximum HP / original maximum HP)`.
Limb HP fits its signed 16-bit field (32767 maximum); absent limbs stay absent.
Main stats round down; limb ratios round to the nearest whole HP.

Changes take effect when loading a save or when the game clears its room cache
on entering a new scene. A pending message appears until then. Room revisits
restore exact live stats and limb maxima from a native sidecar, preserving damage
without scaling again or rolling new limb maxima.

The vanilla room records themselves always stay at their original stat scale,
including on memory cards. Existing saves can be loaded with difficulty enabled;
saves made with it enabled can later be loaded with it disabled or at a different
percentage, and remain usable by the unmodified game. Damaged HP/MP and limbs
are converted proportionally to original units for the card save; whole-number
rounding can lose a little remaining HP/MP on reloading. Positive values never
round to zero. Same-session room revisits retain exact values without this loss.
Linux snapshots preserve the native sidecar and active settings together.

Settings-file keys: `difficulty_base_percent` and `difficulty_clear_percent`.
This first implementation is for rood-boi. PS1 installer inputs are not included.

Validation:

```
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address \
  pc/tests/enemy_difficulty_test.c -o /tmp/enemy_difficulty_test
/tmp/enemy_difficulty_test
GALLIUM_DRIVER=d3d12 ROOD_HEADLESS=egl python3 pc/tests/enemy_difficulty_smoke.py \
  --exe pc/build/boot64_callo/rood_battle_probe --fast --card /path/to/source-card.mcd
```
