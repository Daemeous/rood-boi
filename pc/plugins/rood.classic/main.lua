-- rood.classic (shipped, on): the classic look (ui_menus 0) drawn from Lua (pc/docs/Extensions.md,
-- "The classic look in Lua"; plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md).
--
-- The kinds: the timing word (status), the chain count (chain) and the status display's HP / MP / RISK
-- gauges with their digits and labels (gauges), and Ashley's body/damage silhouette (figure).
-- The status carousel (status_icons), BATTLE MODE notice (battle_mode), and ability_icons are Lua too, and so
-- are the buffs (buff: WEAPON HUMAN +1), the numbers over the actors (damage) and the words over them (actor_word:
-- MISS, the ailments' and buffs' words), quads as the game builds them, the time attack's timer (timer) and the
-- emote balloons over the actors (emote), the text boxes' frames and the name window's band (text_frame, text_band:
-- the game's routines on copies), their text (box_text: each glyph by the game's printer), the portraits and
-- stills (image: the game's tiled drawer) and the targeting list's rows (target_row: a part's name on its plate) and
-- the selected row's leader lines (target_line: the game's line routines), and the area target's marker
-- (target_marker).
-- Ability groups leave pc_weapon_slots_label's weapon-mode names/markers and side effects in C.
-- Each draw function below is a copy of the game's own, made of the game's own pieces (d.text_raw is
-- its raw printer, d.value its digits, d.bar its gauge), so by default it emits exactly the game's
-- packets: the parity gate
-- (pc/tests/classic_parity.py) compares them every frame. What this file may change is the look the
-- classic rule allows: typography (how ui_text sets the game's glyphs: `typeset`, `typography`) and
-- 16:9 placement (`place`); each deliberate difference is named in `look.overrides` (the gate's
-- waivers, pc/tests/classic/waivers.toml, must list the same).
--
-- Edit `look` and save: the running game reloads this file in about half a second. To have something
-- to look at, in the Lua console (F1, Window > Lua console), in the field with the modern UI off:
--   rood.dev.show("status", { timing = "success", variant = 5 })   WELL TIMED!, held until rood.dev.clear()
--   rood.dev.show("status", { timing = "too_slow" })               TOO SLOW!
--   rood.dev.show("chain", { count = 3 })                           3 CHAINS
--   rood.dev.show("buff", { slot = "WEAPON", stat = "HUMAN", value = 1 })  WEAPON HUMAN +1
--   rood.dev.show("damage", { value = 57 })                         57 over Ashley
-- (the gauges are up whenever the field is: M.look.gauges.place.dx = 6 moves them at the next frame)
-- The REPL (pc/tools/rood_repl.py --plugin rood.classic) has this file's module as M: M.look.typeset.margin = 4
-- shows on the held word at the next frame; saving the file brings the file's values back.
-- Turned off (the Lua console's Plugins list), broken, or a kind's hud_classic_<kind> setting off: the
-- game draws them itself (the fallback, typography from ui_text's defaults).
local M = { draw = {} }
local S = rood.settings.get
local function widescreen() return S("widescreen") == true end -- (the 16:9 view; the "edges" anchors' test)

local function make_look()
  return {
    -- The timing word (BATTLE.PRG func_800958A4, the label loop's kind 4): the game's own strings ('$'
    -- centres the word on x; two-word phrases are padded with the font's narrow blanks), colours (PS1
    -- colour words: 0xBBGGRR, 0x80 full) and slide (pixels left of its place at ages 0..3 coming in,
    -- and from `slide_out` on going out, the steps backwards).
    status = {
      place = { anchor = "left", dx = 0, dy = 0 }, -- 16:9: at the screen's left edge (the game's hook's anchor)
      words = { too_fast = "$TOO   FAST!", fast = "$FAST!", slow = "$SLOW!", too_slow = "$TOO   SLOW!" },
      success = { "$COOL!", "$GOOD!", "$EXCELLENT!", "$RIGHT   ON!", "$PERFECT!", "$WELL   TIMED!", "$NICE!",
                  "$GREAT!" },
      colours = { too_fast = 0x808000, fast = 0x608020, success = 0x408040, slow = 0x208060, too_slow = 0x008080 },
      slide = { 64, 16, 4, 1 },
      slide_out = 42,
    },
    -- The chain count (func_80095A4C, kind 5): "3 CHAINS" ("1 CHAIN"), slid in only for the first.
    chain = {
      place = { anchor = "left", dx = 0, dy = 0 },
      one = "$1 CHAIN",
      many = "$%d CHAINS",
      colour = 0x1040A0,
      slide = { 64, 16, 4, 1 },
      slide_out = 87,
    },
    -- The status display's gauges (5BF94.c func_800CA2DC's _renderStatBar calls): HP, MP and RISK, each a
    -- bar, its digits (the current value large, the maximum small with a separator) and its label; with no
    -- risk, RISK ZERO. The game's places (D_800EBBDC, D_800EBBEC: x, y before the display's slide), labels
    -- (the field UI page's HP, MP, RISK: D_800EBBFC, D_800EBC00) and the steps between them as data; the
    -- slots (bytes from vs_scratch.unk4) the game links them into.
    gauges = {
      place = { anchor = "left", dx = 0, dy = 0 }, -- 16:9: the panel's left edge
      pools = {
        { pool = "hp", bar = { 10, 18 }, label = { 10, 10 }, sprite = { page_x = 768, page_y = 0, bits = 4,
          u = 144, v = 0, w = 16, h = 9, clut_x = 864, clut_y = 223 } },
        { pool = "mp", bar = { 80, 18 }, label = { 80, 10 }, sprite = { page_x = 768, page_y = 0, bits = 4,
          u = 160, v = 0, w = 18, h = 9, clut_x = 864, clut_y = 223 } },
        { pool = "risk", bar = { 10, 22 }, label = { 10, 26 }, sprite = { page_x = 768, page_y = 0, bits = 4,
          u = 180, v = 0, w = 26, h = 9, clut_x = 864, clut_y = 223 } },
      },
      -- (steps as the game writes them: packed x | y << 16, added to its packed place: x's carry reaches y)
      maximum = 56 | (-8 << 16),        -- the small maximum's rightmost digit from the bar's place
      separator = 0xFFF9 | (0xFFFE << 16), -- the large current value's from the pen the maximum leaves
      risk_value = 54 | (3 << 16),      -- RISK's value (no maximum) from its bar's place
      digit_step = 5,                   -- the small digits' advance (vs_battle_renderValue: 5 + its font)
      risk_max = 100,
      zero = { text = "RISK  ZERO", x = 12, y = 22, colour = 0x808080 },
      layers = { bars = -12, digits = -16 },
    },
    -- Ashley's silhouette: D_800EBCEC / D_800EBD14 / D_800EBD3C, in the game's limb order.
    -- Each row is {dx, dy, width, height, u, v}; stance 0 then 1. Colours are D_800EBC54,
    -- indexed by the game's read-only condition (critical, damaged, wounded, good, excellent).
    figure = {
      place = { anchor = "left", x = 8, y = 184, dx = 0, dy = 0 }, layer = -4,
      colours = { 0x180058, 0x004070, 0x087078, 0x186820, 0x687018 },
      stances = {
        { {8,8,12,20,16,184}, {24,8,12,18,32,160}, {16,0,12,12,0,160},
          {16,8,12,14,0,172}, {8,16,28,24,44,160} },
        { {0,8,18,20,12,160}, {24,8,16,22,32,184}, {16,0,12,12,0,160},
          {16,8,12,14,0,172}, {8,16,28,24,44,160} },
      },
      sprite = { page_x = 768, page_y = 0, bits = 4, clut_x = 832, clut_y = 223 },
    },
    ability_icons = {
      place = { anchor = "centre", dx = 0, dy = 0 }, layer = "unk8", right_x = 160,
      -- marker x/y, label x/y/w/h/u/v; IDs 0..3 right, 4..7 left.
      labels = {
        {77,101,82,99,47,9,52,101}, {53,115,-2,113,58,9,100,101},
        {87,113,92,111,52,9,0,101}, {63,127,7,125,58,9,160,101},
        {89,101,94,99,34,9,0,113}, {70,115,27,112,57,9,152,113},
        {98,113,103,111,45,9,104,113}, {80,127,16,124,67,9,36,113},
      },
      marker = { w = 6, h = 6, u = 192, v = 128 },
      face_xy = { 0x640041, 0x6D0038, 0x6D004A, 0x760041 },
      face = { size = 16, u_step = 16, v = 128 },
      cross = { x = 72, y = 102, w = 30, h = 30, u = 184, v = 160 },
      label_sprite = { page_x = 448, page_y = 256, bits = 4, clut_x = 976, clut_y = 220 },
      button_sprite = { page_x = 768, page_y = 0, bits = 4, clut_x = 944, clut_y = 223 },
      full_colour = 0x808080, semi_alpha = 0.5,
      -- C decides eligibility; its codes select independent presentation styles.
      availability = {
        [0x140] = { colour = 0x404040, alpha = 0.5 },
        [0x180] = { colour = 0x808080, alpha = 0.5 },
      },
    },
    battle_mode = {
      place = { anchor = "right", dx = 0, dy = 0 }, layer = -12,
      x = 236, y = -4, step_y = 3, colour = 0x404040, alpha = 0.5,
      sprite = { page_x = 448, page_y = 256, bits = 4, u = 64, v = 0,
        w = 73, h = 10, clut_x = 976, clut_y = 220 },
    },
    -- func_800C9CB4: clipped carousel cells and the count/selection indicators.
    status_icons = {
      place = { anchor = "left", dx = 0, dy = 0 }, layers = { icons = -4, indicators = -8 },
      x = 160, y = 8, cell = 16, supplemental_h = 8,
      supplemental_w = 8, supplemental_stride = 8,
      supplemental_alignment = 8, -- positive power of two: atlas clipping masks
      texels = { 0x1010,0x1020,0x1030,0x1040,0x1050,0x1060,0x1080,0x1090,
        0x10A0,0x10B0,0x10C0,0x10D0,0x10E0,0x10F0,0x2000,0x2010,
        0x30B8,0x30B8,0x30B8,0x30B8,0x3098,0x3098,0x3098,0x3098,
        0x30C8,0x30C8,0x30C8,0x30C8,0x30A8,0x30A8,0x30A8,0x30A8 },
      alternate_clut = 0x0F0F906A, cluts = { 0x37F8, 0x37F9 },
      supplemental_uv = 0x3068, supplemental_clut = 0x37FF,
      indicator = { x = 148, y = 8, step = 3, row_step = 3, size = 4,
        columns = 8, -- positive power of two; selection deliberately tests this bit (not row division)
        strip_padding = 1, strip_from = 3,
        dot_uv = 0x0C1A, strip_uv = 0x0C00, clut = 0x37F4 },
      sprite = { page_x = 768, page_y = 0, bits = 4, bgr = 0x808080 },
    },
    -- An equipment's class or affinity change (2842C.c func_8009695C, the label loop's kind 6): WEAPON HUMAN +1,
    -- textured quads of the field UI page at 8, 172 less the record's rise (10 for each buff shown after it).
    -- The words' texels (D_80069144 by equipment, D_8006917C by class / affinity: u, v, w, h), the colour
    -- words (a gain, a loss), the gaps between the pieces, the sign's and the small digits' cells.
    buff = {
      place = { anchor = "left", dx = 0, dy = 0 }, layer = 8,
      slots = { WEAPON = { 72, 160, 32, 8 }, SHIELD = { 76, 168, 28, 8 }, ["R.ARM"] = { 78, 176, 26, 8 },
        ["L.ARM"] = { 78, 184, 26, 8 }, HEAD = { 82, 192, 22, 8 }, BODY = { 82, 200, 22, 8 },
        LEGS = { 82, 208, 22, 8 } },
      stats = { HUMAN = { 104, 160, 28, 8 }, BEAST = { 104, 168, 26, 8 }, UNDEAD = { 104, 176, 30, 8 },
        PHANTOM = { 104, 184, 36, 8 }, DRAGON = { 104, 192, 32, 8 }, EVIL = { 104, 200, 18, 8 },
        PHYSICS = { 144, 160, 30, 8 }, AIR = { 144, 168, 14, 8 }, FIRE = { 144, 176, 18, 8 },
        EARTH = { 144, 184, 26, 8 }, WATER = { 144, 192, 26, 8 }, LIGHT = { 144, 200, 22, 8 },
        DARKNESS = { 144, 208, 36, 8 } },
      colours = { gain = 0x804000, loss = 0x004080 },
      gap = 2,                                                       -- after each word
      sign = { dy = -1, w = 8, h = 8, gain_u = 128, loss_u = 120, v = 208, after = 2 },
      digits = { dy = -1, w = 6, h = 10, step = 5, u_step = 6, v = 0 },
      words = { page_x = 768, page_y = 0, bits = 4, clut_x = 848, clut_y = 223 },
      figures = { page_x = 768, page_y = 0, bits = 4, clut_x = 832, clut_y = 223 },
    },
    -- A number over an actor (func_80094B0C, kind 1): damage, healing, MP; a stat's change (func_80094E18:
    -- its icon of the combat labels (D_80068FAC by D_800E861C: u, v, w), its sign, the digits, drifting
    -- by half the age). The large digits' cells, colour words by pool (a loss, a gain), the bounce by frame
    -- (a gain from frame 0, a loss from frame 4; each digit to the left half the one before).
    damage = {
      place = { anchor = "centre", dx = 0, dy = 0 }, layer = -20,
      colours = { hp = { loss = 0x808080, gain = 0x008000 }, mp = { loss = 0x800080, gain = 0x808000 },
        stat = { loss = 0x808080, gain = 0x808080 } },
      bounce = { 0, -6, -12, -14, -16, -10, -5, -1, 0, -3, -6, -7, -8, -5, -3, -1, 0, -2, -1, 0 },
      loss_frame = 4, last_frame = 15,
      digits = { w = 8, h = 12, step = 6, u = 64, u_step = 8, v = 0, half = 6, centre = 3, clip = 328 },
      figures = { page_x = 768, page_y = 0, bits = 4, clut_x = 832, clut_y = 223 },
      stat = {
        icons = { { 102, 76, 16 }, { 220, 100, 18 }, { 0, 64, 24 }, { 24, 64, 20 }, { 44, 64, 24 }, { 152, 52, 26 } },
        h = 12, half = 6, padding = 12, -- (its width: the digits', the icon's and this, halved)
        icon = { page_x = 448, page_y = 256, bits = 4, clut_x = 976, clut_y = 220, raw = true },
        sign = { gap = 4, dy = -5, w = 8, h = 10, gain_u = 162, loss_u = 154, v = 76,
          page_x = 768, page_y = 0, bits = 4, clut_x = 864, clut_y = 223, raw = true },
      },
    },
    -- A word over an actor (2842C.c's label loop, slot -20), the draw the game chose. MISS (func_8009506C): the
    -- info capitals M, I, S, S (D_800E8640: u, v, w; 10 texels high) from 16 pixels left of its place, squashed
    -- open over its first ages and shut over its last (D_800E8634: half its height by step), raw and half
    -- blended. A combat label (func_80095258): the ailments' and buffs' words, one or two pieces of the combat
    -- labels' page (D_800690B4: its colour, then its pieces; `pieces` is D_80068FAC: u, v, w; 12 high), centred
    -- on its place, sliding in from the right and out to the left (`slide`, by its first and last ages), its
    -- colour (D_80069134: r, g, b) faded in (`fade` / 32), held at half plus 96 (`lit`), then half; a lowered
    -- stat's word (`lowered`) does not slide, takes the first colour and shows the arrow (func_800957D0)
    -- from age 15, its cell stepping right to the fourth.
    actor_word = {
      place = { anchor = "centre", dx = 0, dy = 0 }, layer = -20,
      enter = 4, leave = 27, life = 30,                -- (ages: the first held step, the first leaving one, 0 at)
      miss = { dx = -16, letters = { { 100, 86, 11 }, { 70, 86, 4 }, { 160, 86, 9 }, { 160, 86, 9 } }, h = 10,
        squash = { 1, 2, 4, 5, 5 }, alpha = 0.5,
        sprite = { page_x = 768, page_y = 0, bits = 4, clut_x = 864, clut_y = 223, raw = true } },
      labels = { { 0, 31 }, { 0, 29, 27 }, { 0, 30, 27 }, { 0, 28, 26 }, { 0, 32, 27 }, { 0, 1, 5 }, { 1, 1, 4 },
        { 0, 2, 5 }, { 1, 2, 4 }, { 0, 3, 5 }, { 1, 3, 4 }, { 2, 7 }, { 2, 8 }, { 2, 9 }, { 2, 10 }, { 2, 11 },
        { 2, 12 }, { 3, 13 }, { 3, 14 }, { 0, 15, 5 }, { 1, 15, 4 }, { 2, 16, 19 }, { 2, 16, 20 }, { 2, 16, 21 },
        { 2, 16, 22 }, { 2, 17, 19 }, { 2, 17, 20 }, { 2, 17, 21 }, { 2, 17, 22 } },
      pieces = { { 0, 64, 36 }, { 0, 64, 24 }, { 24, 64, 20 }, { 44, 64, 24 }, { 224, 64, 16 }, { 212, 76, 32 },
        { 150, 64, 26 }, { 68, 64, 40 }, { 108, 64, 30 }, { 138, 64, 50 }, { 188, 64, 36 }, { 0, 76, 52 },
        { 52, 76, 30 }, { 144, 88, 58 }, { 120, 76, 60 }, { 180, 76, 30 }, { 136, 88, 8 }, { 104, 88, 32 },
        { 76, 88, 40 }, { 0, 88, 16 }, { 20, 88, 20 }, { 40, 88, 30 }, { 72, 88, 32 }, { 102, 76, 16 },
        { 220, 100, 18 }, { 152, 52, 26 }, { 182, 40, 30 }, { 214, 40, 24 }, { 152, 52, 28 }, { 180, 52, 40 },
        { 220, 52, 30 }, { 108, 64, 30 }, { 216, 28, 28 } },
      colours = { { 208, 208, 208 }, { 64, 192, 255 }, { 255, 208, 48 }, { 160, 224, 96 } },
      h = 12, half = 6, alpha = 0.5,
      slide = { 32, 14, 6, 2 }, fade = { 5, 9, 13, 15 }, lit_until = 6, lit_from = 25, lit = 96,
      fading = { page_x = 448, page_y = 256, bits = 4, abr = 1, clut_x = 960, clut_y = 220 },
      held = { page_x = 448, page_y = 256, bits = 4, clut_x = 976, clut_y = 220 },
      arrow = { from = 15, steps = 4, dx = -8, dy = -5, w = 16, h = 10, u = 64, u_step = 16, v = 48,
        sprite = { page_x = 448, page_y = 256, bits = 4, clut_x = 992, clut_y = 220, raw = true } },
    },
    -- The time attack's timer (5BF94.c _renderTimer, while the lower screen shows it): the game's string
    -- (mm:ss:hh, its digits the game's) in the info capitals at 128, 200, grey, slot -4.
    timer = { place = { anchor = "centre", dx = 0, dy = 0 }, layer = -4, x = 128, y = 200, colour = 0x808080 },
    -- An emote over an actor (2842C.c func_80095C18), linked into its actor's depth slot (the layer the
    -- kind's own). The balloon (D_80068F04's corners and D_80068F64's texels by side: 0, 1 the speech balloon
    -- facing either way, 2, 3 the shout, 4, 5 the surprise strokes), then its marks from the place by side
    -- (D_80068F94, packed as the game adds them): one to three dots, '!' in the shout, the heart; '?' alone
    -- sets its own page. An off-screen actor's arrow (func_8009651C, slot -16): eight points about the clamped
    -- place, turned by the direction (1 left, 2 right, 4 up, 8 down and their pairs). (A far actor's sprite,
    -- the kind's bit 8, stays the game's: the gate has no scene that shows one.)
    emote = {
      place = { anchor = "centre", dx = 0, dy = 0 },
      corners = { { -6, -22, 18, -22, -6, 1, 18, 1 }, { -17, -22, 7, -22, -17, 1, 7, 1 },
        { -10, -29, 20, -29, -10, 1, 20, 1 }, { -19, -29, 9, -29, -19, 1, 9, 1 },
        { -9, -16, 11, -16, -9, -3, 11, -3 }, { -10, -16, 10, -16, -10, -3, 10, -3 } },
      uvs = { { 64, 16, 88, 16, 64, 39, 88, 39 }, { 87, 16, 63, 16, 87, 39, 63, 39 },
        { 121, 17, 151, 17, 121, 47, 151, 47 }, { 150, 17, 120, 17, 150, 47, 120, 47 },
        { 160, 17, 180, 17, 160, 30, 180, 30 }, { 179, 17, 159, 17, 179, 30, 159, 30 } },
      marks = { -983042, -983054, -1376256, -1310727, -720893, -720893 },
      balloon = { page_x = 448, page_y = 256, bits = 4, clut_x = 992, clut_y = 220, bgr = 0x808080 },
      dot = { step = 6, sprite = { page_x = 448, page_y = 256, bits = 4, u = 90, v = 18, w = 5, h = 5,
        clut_x = 992, clut_y = 220, bgr = 0x808080, set_page = false } },
      shout = { page_x = 448, page_y = 256, bits = 4, u = 152, v = 17, w = 7, h = 12, clut_x = 992, clut_y = 220,
        bgr = 0x808080, set_page = false },
      heart = { offset = 2 | (-3 << 16), sprite = { page_x = 448, page_y = 256, bits = 4, u = 152, v = 32, w = 16,
        h = 16, clut_x = 992, clut_y = 220, bgr = 0x808080, set_page = false } },
      question = { offset = -22 << 16, sprite = { page_x = 448, page_y = 256, bits = 4, u = 183, v = 17, w = 7, h = 12,
        clut_x = 992, clut_y = 220, bgr = 0x808080 } },
      arrow = { dx = { -8, 0, 7, 10, 7, 0, -8, -11 }, dy = { -8, -11, -8, 0, 7, 10, 7, 0 },
        directions = { [4] = 0, [6] = 1, [2] = 2, [10] = 3, [8] = 4, [9] = 5, [1] = 6, [5] = 7 },
        quad = { page_x = 768, page_y = 0, bits = 4, clut_x = 976, clut_y = 223, raw = true,
          corners = { 0, 0, 0, 0, 0, 0, 0, 0 }, uvs = { 48, 48, 63, 48, 48, 63, 63, 63 } } },
    },
    -- A text box's frame (5BF94.c func_800CD3E4: the talk bubbles' and the information box's shapes), drawn by
    -- the game's own routine on a copy of the box (d.frame): its place this frame, size, shape, flip and the
    -- speaker's tail are the game's (the opening's zoom included). (The vector talk bubbles, ui_bubbles vector,
    -- an option, redraw these copies as they do the game's.)
    -- (place: anchor "game", widescreen placing the box as it places the game's own 2D: the edge extension too)
    text_frame = { place = { anchor = "game", dx = 0, dy = 0 } },
    -- A text box's band across the screen (func_800CD158: the name window's): the picture behind it darkened row by
    -- row, black past it, the action's icon once settled, by the game's own routine on a copy of the box (d.band).
    text_band = { place = { anchor = "game" } }, -- (across the screen: widescreen stretches it to the edges as the game's)
    -- A text box's text as the game's printer set it this frame (_printVariableWidthFont: the typewriter, pages, the
    -- continuation arrow, sounds and buttons are the game's): each glyph again by the same printer (d.char), with
    -- the font table, colour style and brightness it had. Its typography is ui_text's (the dialogue and menu
    -- styles; ui_text_layout, ui_text_bubble_align: the settings).
    box_text = { place = { anchor = "game", dx = 0, dy = 0 } },
    -- A portrait or still (func_800CCA90; the opening's cards), by the game's own tiled drawer as the game calls it,
    -- placed as the game's (its full-screen stills fill 16:9 as the drawer itself decides).
    image = { place = { anchor = "game" } },
    -- A row of the targeting list (58578.c func_800C1DC4): the part's name by the raw printer (dx from the row's x:
    -- `left` on the screen's left half, `right` on the other; one up), grey or, selected, white; under it the plate:
    -- a gradient across w x h between two colours of a bank (vs_battle_uiGradientStop: each bank's `near` and `far`
    -- by _lerpColor, the row's fade step between them, selected the last step; mirrored on the left half), and its
    -- black shadow (dx, dy from the plate; on the left half `wider` more to the left).
    target_row = {
      -- anchor "game": as the game's own 2D (16:9: centre-compressed, the rows slide in from the 4:3 edges);
      -- "edges": 16:9 only, a row on the left half anchored to the 16:9 left edge, on the right half to the right
      place = { anchor = "game", dx = 0, dy = 0 },
      half = 160,
      name = { left = 66, right = 6, dy = -1, colour = 0x404040, selected = 0x808080 },
      plate = { w = 72, h = 8, steps = 8, brightness = 0x80 },
      near = { 0x6B4100, 0x6C8219 }, far = { 0x330500, 0x262801 }, -- (bank 0's, bank 1's: PS1 colour words)
      shadow = { dx = 2, dy = 2, wider = 2 },
    },
    -- A selected row's leader lines (func_800C16FC, on the left half func_800C1A40): from the part's point to the row
    -- (`ring`: from the row's place, by the screen's half), swept in by their age (the count `lag` behind it), and
    -- from `trail` on a second one behind (its count the age plus `step`, wrapped at `cycle`).
    target_line = {
      -- anchor "edges" with target_row's "edges": the lines stay the game's (they start at the part, centre-
      -- compressed) and their row end moves out by `edge_shift` (16:9: a row anchored to an edge lands 320 / 6 =
      -- 53.3 game pixels further out than centre-compressed, gpu_hw.c anchor_x)
      place = { anchor = "game", dx = 0, dy = 0 },
      edge_shift = 53,
      half = 160,
      ring = { left = 71, right = 0, dy = 4 },
      lag = 2, trail = 18, step = 14, cycle = 32,
    },
    -- The area target's marker (func_800C28AC) at the point the game projected: unless the pitch is locked (`lock`),
    -- the brackets (`brackets`: two quads of the field UI page, mirrored, their corners packed as the game adds
    -- them), then three sprites (`sprites`: the step packed, w, h, u, v; D_800EA670, D_800EA684, D_800EA698: from
    -- the first, or locked from the third), the one at `pulse_at` by the brightness step (`pulse`:
    -- vs_battle_cursorBrightnessAnimation), the others at `brightness`. Slot -4.
    target_marker = {
      place = { anchor = "game", dx = 0, dy = 0 },
      layer = -4,
      brackets = {
        { deltas = { 0xFFF7FFF3, 0xFFF7FFFD, 0xFFFEFFF3, 0xFFFEFFFD }, uvs = { 241, 56, 231, 56, 241, 63, 231, 63 } },
        { deltas = { 0x0001FFF3, 0x0001FFFD, 0x0008FFF3, 0x0008FFFD }, uvs = { 241, 64, 231, 64, 241, 71, 231, 71 } },
      },
      bracket = { page_x = 768, page_y = 0, bits = 4, clut_x = 976, clut_y = 223, bgr = 0x808080,
        corners = { 0, 0, 0, 0, 0, 0, 0, 0 }, uvs = { 0, 0, 0, 0, 0, 0, 0, 0 } },
      sprites = { { 0xFFF80004, 10, 7, 232, 56 }, { 0x00020004, 10, 7, 232, 64 }, { 0xFFFCFFFD, 7, 7, 232, 48 },
        { 0xFFF0FFFC, 9, 11, 220, 48 }, { 0x0004FFFC, 9, 11, 220, 60 } },
      locked = 2, -- (the sprites' first index when locked)
      sprite = { page_x = 768, page_y = 0, bits = 4, clut_x = 976, clut_y = 223, u = 0, v = 0, w = 0, h = 0,
        bgr = 0x808080 },
      brightness = 0x80, pulse_at = 3,
      pulse = { 128, 184, 240, 232, 224, 216, 208, 200, 192, 184, 176, 168, 160, 152, 144, 136 },
    },
    -- MAINMENU's shared frame (58EC.c), every menu screen's (the Option screen's toured: the game's rows and
    -- flyouts, our pages' rows). Each piece is a copy of the game's own drawing: placed as the game's 2D is
    -- (anchor "game": 16:9 as today); dx, dy move a piece (a placement override).
    -- A row of vs_battle_menuItems (its window, label, counts, icons; _renderMenuItems' emit by the game's own
    -- code on a copy of the row: d.menu_row).
    menu_row = { place = { anchor = "game", dx = 0, dy = 0 } },
    -- The pointing hand (vs_mainMenu_renderCursor): a sprite of the menu page, pulsing by its step
    -- (vs_battle_cursorBrightnessAnimation), the CLUT by its colour (0 white, 1 the other).
    menu_cursor = {
      place = { anchor = "game", dx = 0, dy = 0 },
      sprite = { page_x = 768, page_y = 0, bits = 4, u = 32, v = 48, w = 16, h = 16, clut_x = 896, clut_y = 223,
        bgr = 0x808080 },
      cluts = { 896, 928 },
      pulse = { 128, 184, 240, 232, 224, 216, 208, 200, 192, 184, 176, 168, 160, 152, 144, 136 },
    },
    -- The command title (_renderMenuCommand): the action's sprite (uvs, sizes: its tables) right-aligned at
    -- `right`, sliding down to y 12 - 4, semi-transparent.
    menu_command = {
      place = { anchor = "game", dx = 0, dy = 0 },
      right = 312, top = -4,
      sizes = { { 62, 10 }, { 36, 10 }, { 30, 10 }, { 56, 10 }, { 30, 10 }, { 32, 10 } }, -- actions 1..6 (w, h)
      uvs = { { 0, 64 }, { 64, 64 }, { 100, 64 }, { 132, 64 }, { 188, 64 }, { 0, 74 } },
      sprite = { page_x = 768, page_y = 0, bits = 4, abr = 1, u = 0, v = 0, w = 0, h = 0, clut_x = 848, clut_y = 223,
        bgr = 0x808080 },
    },
    -- The tab arrows (vs_mainMenu_renderTabNavigation): mode 1 the L1 / R1 labels and the arrows by them, mode 0
    -- the arrows at the list's corners; bobbing by step // 4; lit (animating) or dim.
    menu_tabs = {
      place = { anchor = "game", dx = 0, dy = 0 },
      labels = { { "1", 216, 8 }, { "L", 210, 8 }, { "1", 288, 8 }, { "R", 282, 8 } },
      colours = { [false] = 0x404040, [true] = 0x808080 },
      arrows = { [0] = { { 16, 16 }, { 162, 17 } }, [1] = { { 200, 5 }, { 292, 6 } } },
      sprite = { page_x = 768, page_y = 0, bits = 4, u = 0, v = 48, w = 16, h = 16, clut_x = 912, clut_y = 223,
        bgr = 0x808080 },
      cluts = { [false] = 912, [true] = 896 },
    },
    -- The background (_renderBackground): the menu picture (two 8-bit sprites, 256 + 64 wide) at the fade's
    -- brightness (fade * 8; semi-transparent until 16) and, while fading, the frame behind it darkened (two
    -- 15-bit sprites of the other buffer, at 128 - fade * 8). In its own slot.
    menu_background = {
      place = { anchor = "game", dx = 0, dy = 0 },
      picture = { { 256, 64, 768 }, { 0, 256, 640 } }, -- x, w, page_x
      sprite = { page_x = 640, page_y = 256, bits = 8, abr = 1, u = 0, v = 0, w = 0, h = 240, clut_x = 768,
        clut_y = 227, bgr = 0x808080 },
      behind = { page_x = 0, page_y = 0, bits = 15, abr = 1, u = 0, v = 0, w = 0, h = 240, bgr = 0x808080 },
    },
    -- Our Option pages' value column (pc/src/ui/classic_options.c's draws: the game's menu font and gauge; its
    -- numbers are rood.options' M.style, read as the page draws them): ON / OFF, a choice between arrows, a
    -- slider's gauge and value, a binding's or an info's text, a link's arrow, a note.
    option_value = { place = { anchor = "game", dx = 0, dy = 0 }, arrow_gap = 2, note_x = 6, bar_room = 8 },
    -- The Items screen's own panels (MENU3). The category icons (items0.h _renderItemCategory): eight 16 x 16 icons
    -- from x = left + ((i + 1) % 8) * gap (the Manage page's first), at the loop's step (y); the selected one in
    -- `lit` CLUT, the others `dim`; with the highlight on, the selected one raised to `raised_y` (unk8) or handed to
    -- row 31 (drawn by it). Their texels from u = u0 + i * 16, v. (Storage draws its own copy: the same, the dim
    -- icons in its slot, e.items.slot.)
    items_category = {
      place = { anchor = "game", dx = 0, dy = 0 },
      left = 32, gap = 16, raised_y = 16, u0 = 120, v = 128, cluts = { lit = 976, dim = 992 },
      sprite = { page_x = 768, page_y = 0, bits = 4, u = 0, v = 0, w = 16, h = 16, clut_x = 992, clut_y = 223,
        bgr = 0x808080 },
    },
    -- MAINMENU's equipment panels as Items draws them (C48.c). The stats panel (vs_mainMenu_renderEquipStats): from
    -- its place (slid by the game), ORG/EQP over RANGE and RISK (when shown; else ORG/EQP lower), then STR / INT /
    -- AGI (their names by the subtype: the item's own, ATTACK or DEFENSE), each with its value and the equipped
    -- total at `columns`; numbers above or below their threshold in `up` / `down`; "#??" when not known.
    equip_stats = {
      place = { anchor = "game", dx = 0, dy = 0 },
      layer = -24, line = 10, header_x = 4, columns = { 28, 60 }, colour = 0x808080, up = 0x804020,
      down = 0x204080, header = "ORG/EQP", unknown = "#??", range_risk = { "#RANGE", "#RISK" },
      names = { [0] = "#STRENGTH", "#INTELLIGENCE", "#AGILITY", false, "#ATTACK/STR", "#ATTACK/INT", "#AGILITY",
        false, "#DEFENSE/STR", "#DEFENSE/INT", "#AGILITY" },
    },
    -- The DP / PP bars (vs_mainMenu_renderDpPpBars): sliding down by the step (8 a step, from -8), each a word, its
    -- maximum (small digits), the slash and the current value (large), and its gauge 8 lower.
    dp_pp_bars = {
      place = { anchor = "game", dx = 0, dy = 0 },
      layer = -12, colour = 0x808080,
      bars = { { word = "DP", x = 10, digits = 66, gauge = 0 }, { word = "PP", x = 80, digits = 136, gauge = 1 } },
    },
    -- A grip's gem slots (items1.h _gripStatusView): the gem row icon (vs_mainMenu_renderMenuRowIcon: unk8 + 4) and
    -- "X     n" by the raw printer.
    gem_slots = {
      place = { anchor = "game", dx = 0, dy = 0 },
      icon = { 256, 32 }, text = { 280, 36 }, prefix = "X     ", colour = 0x808080,
      sprite = { page_x = 768, page_y = 0, bits = 4, u = 32, v = 96, w = 16, h = 16, clut_x = 992, clut_y = 223,
        bgr = 0x808080 },
    },
    -- The class / affinity / type table (C48.c _renderStatPage), sliding in by x_offset (0 or less): per row its
    -- name, its value (and, comparing, the difference with + or *), a gauge of four shaded lines (`positive` or
    -- `negative`, 24 pixels a hundred), its frame (`frame`, `frame_light`), the box's shading (`shade`, `shade_dark`)
    -- and the draw modes between; then the Square button over its window and the header CLASS / AFFINITY / TYPE in
    -- the text boxes' font, each name in its style (1 shown, 0 enabled, 3 not; `styles` + the font's 4).
    stat_page = {
      place = { anchor = "game", dx = 0, dy = 0 },
      rows = { [0] = { 0, 8, 0x30, 0x60 }, { 16, 24, 56, 0x70 }, { 33, 33, nil, 48 } }, -- each page: its values,
      -- the compared ones, the equipped ones (offsets in the stats) and its rows' extent (16 a row)
      top = 0x40, name_x = 0x40, value_x = 88, diff_x = 0xA4, gauge_x = 116,
      white = 0x808080, grey = 0x404040, up = 0x804020, down = 0x204080,
      positive = { 0x40817318, 0x40D0E030, 0x40D0E030, 0x40817318 },
      negative = { 0x40693F6E, 0x409F78DC, 0x409F78DC, 0x40693F6E },
      frame = 0x40D0E030, frame_light = 0x40978918, shade = 0x40330500, shade_dark = 0x405D3200,
      line = { 0x50330500, 0x5D3200 }, modes = { 0xE1000000, 0xE1000200 },
      names = { [0] = "#HUMAN", "#BEAST", "#UNDEAD", "#PHANTOM", "#DRAGON", "#EVIL", false, false, "#PHYSICAL",
        "#AIR", "#FIRE", "#EARTH", "#WATER", "#LIGHT", "#DARK", false, "#BLUNT", "#EDGED", "#PIERCING" },
      window = { 24, 48, 135, 12 }, button = { 16, 46 },
      sprite = { page_x = 768, page_y = 0, bits = 4, u = 16, v = 128, w = 16, h = 16, clut_x = 944, clut_y = 223,
        bgr = 0x808080 },
      header = { x = 0x22, y = 0x30, parts = { { "CLASS", 0 }, { "/" }, { "AFFINITY", 1 }, { "/" }, { "TYPE", 2 } } },
      styles = { shown = 1, enabled = 0, off = 3 }, font_style = 4,
    },
    -- The STATUS strip (16C.c): the button's window (vs_mainmenu_renderButtonBackground), the Square button
    -- (vs_mainmenu_renderButton: its texels by the button's id) and the word, lit by the state (colour << state),
    -- all sliding in by `row` (x - row).
    items_status_button = {
      place = { anchor = "game", dx = 0, dy = 0 },
      window = { 16, 38, 88, 10 }, button = { 8, 36 }, word = { "STATUS", 28, 38 }, colour = 0x202020,
      sprite = { page_x = 768, page_y = 0, bits = 4, u = 16, v = 128, w = 16, h = 16, clut_x = 944, clut_y = 223,
        bgr = 0x808080 },
    },
    -- The OK mark beside a menu's last choice (MENUC _drawOk: Setup's Assemble, Gems, Combine; MENUD's top menu): the
    -- word at the game's place, in the grey of its pulse (`grey` times the level the game steps), in `layer`.
    ok_mark = { place = { anchor = "game", dx = 0, dy = 0 }, word = "OK", grey = 0x010101, layer = -12 },
    -- A misc item's discard amount (items1.h _discardMenu's picker, MENUD's copy): its three digits in the text boxes'
    -- font (`style`), right to left from the game's place, `step` apart, in the slot after unk8 (the printer's own).
    discard_count = { place = { anchor = "game", dx = 0, dy = 0 }, style = 5, step = 6 },
    -- The rename grid (MENU8 _renderRenameMenu), sliding in from the right by x_offset (the left column out to the left):
    -- the buttons (`buttons`: which, x from the left edge, y) and the pad's sprite (`pad`); each command's bar, `bar.rows`
    -- shaded lines `bar.length` long from y `bar.top` (`bar.line` a command), fading by its fade (the game's
    -- gradient stop between `bar.near` and `bar.far`: the menu rows' first colours), between its draw `modes`
    -- (`bar.code` the shaded line's); the commands' words (the record's, Insert first, from `words.x`, `words.step`
    -- apart, upward from `words.y` by `words.line`) and the name typed (`name`: its pen from x, `step` a code, the
    -- game's blank `blank` skipped) in the text boxes' font (`style`); at rest the L1 / R1 arrows (`arrows`: their
    -- digits and letters by the raw printer, `lit` or `dim`; their sprites stepping out by the record's step, the
    -- CLUT `clut_lit` or `clut_dim`) and the name's window (`window`: the game's words, a shaded polygon); the
    -- character table (`table`: from x, `step` across, `line` down); the background's four shaded polygons
    -- (`background`: their colour words and corners, each slid with the grid).
    rename_grid = {
      place = { anchor = "game", dx = 0, dy = 0 },
      buttons = { { "cross", 16, 50 }, { "square", 16, 66 }, { "triangle", 16, 82 } },
      pad = { x = 16, y = 34, sprite = { page_x = 768, page_y = 0, bits = 4, u = 176, v = 112, w = 16, h = 16,
        clut_x = 944, clut_y = 223, bgr = 0x808080 } },
      bar = { top = 20, line = 16, rows = 12, length = 96, near = { 0x6B4100, 0x6C8219 }, far = { 0x330500, 0x262801 },
        brightness = 0x80, code = 0x50000000, modes = { 0xE1000200, 0xE1000000 } },
      words = { x = 40, y = 84, line = 16, step = 6 },
      name = { x = 162, y = 48, step = 6, blank = 0x8F },
      style = 4,
      arrows = { lit = 0x808080, dim = 0x404040, y = 51,
        left = { { "1", 152 }, { "L", 146 }, x = 136, u = 0 }, right = { { "1", 292 }, { "R", 286 }, x = 296, u = 16 },
        sprite_y = 48, clut_lit = 896, clut_dim = 912,
        sprite = { page_x = 768, page_y = 0, bits = 4, u = 0, v = 48, w = 16, h = 16, clut_x = 912, clut_y = 223,
          bgr = 0x808080 } },
      window = { 0xE1000200, 0x38000000, 0x001E00D0, 0, 0x002000E8, 0xC0A080, 0x002E00E8, 0, 0x001E0100 },
      table = { x = 130, y = 78, step = 12, line = 16 },
      background = { mode = 0xE1000200, top = 0x386B4100, colour = 0x330500,
        corners = { { 216, 72 }, { 142, 180 }, { 112, 72 }, { 112, 224 }, { 216, 72 }, { 216, 224 }, { 142, 180 },
          { 112, 224 }, { 216, 72 }, { 290, 180 }, { 216, 224 }, { 320, 224 }, { 216, 72 }, { 320, 72 }, { 290, 180 },
          { 320, 224 } } },
    },
    -- A highlight box on the rename grid (21A0.c _highlightSelection): a semi-transparent shaded polygon, its draw mode
    -- (`mode`), the top-left's colour word (`code`: with the polygon's code), the bottom-left's (`colour`); the others
    -- black.
    rename_box = { place = { anchor = "game", dx = 0, dy = 0 }, mode = 0xE1000220, code = 0x3AC0A080,
      colour = 0xC0A080 },
    -- MENU7's file menu (260.c _drawFileMenu: the save list after Storage, Data's SAVE / LOAD), drawn at once. `font`:
    -- its printer's (_printCharacter: 12 x 12 cells, `columns` a row of the page at page_x, the CLUT row clut_y from
    -- clut_x, 16 a colour; `widths` its advances by code, as runs of (width, count); `space` the blank code, `arrow`
    -- the blinking arrow's last code, `line` a newline's step, `mode` / `colour` its sprite's draw mode and colour).
    -- `info`: the save info's sprites (_drawSaveInfoUI: colon, No., MAP, SAVE, CLEAR, TIME, %, HP, MP, the dot; each
    -- its texels, CLUT and size), `digits` (_renderInteger: 6 x 10 from u 0, 5 apart) and `hp_digits` (_drawHPMP's
    -- current value: 7 x 12 from u 64, 8 a digit, 6 apart); the panels' gradient (`panel`: inner colours by the blend
    -- step, its two highlighted pairs; their draw mode and the shadow tile), the gauges (`gauge`: HP's and MP's
    -- colours, the full one's, its back tile), the location pictures (`picture`: 64 x 32 by location, the CLUT row
    -- read, swapped and put back), the frame (`frame`: the band, its shaded rows, the caution, the message tile, the
    -- hand, the fade) and the progress animations (`progress`). Places are the game's (320 x 240, no anchor: the
    -- file menu is drawn as the game's own 2D is).
    file_menu = {
      font = { page_x = 832, columns = 21, cell = 12, clut_x = 896, clut_y = 222, mode = 0xE100002D,
        colour = 0x66808080, space = 0x8F, arrow = 0xBC, line = 13,
        widths = { 6, 62, 12, 1, 6, 22, 12, 1, 6, 88, 12, 15, 8, 1, 5, 1, 7, 1, 8, 1, 7, 3, 6, 3, 7, 3, 9, 1, 7, 4, 6, 1, 7, 2, 6, 1, 8, 1, 7, 1, 8, 2, 9, 1, 8, 2, 4, 1, 7, 1, 5, 1, 7, 2, 6, 1, 8, 1, 7, 3, 6, 1, 8, 1, 6, 1, 8, 2, 7, 1, 6, 1, 7, 1, 6, 1, 8, 2, 7, 2, 8, 1, 7, 1, 5, 1, 6, 1, 7, 1, 6, 1, 7, 3, 8, 1, 12, 1, 7, 9, 6, 4, 7, 9, 12, 1, 7, 5, 8, 4, 7, 12, 12, 27, 6, 1, 12, 6, 8, 1, 12, 1, 6, 1, 5, 1, 4, 1, 12, 4, 4, 1, 6, 2, 12, 2, 6, 2, 3, 3, 4, 1, 6, 1, 12, 1, 6, 3, 12, 1, 5, 1, 6, 1, 12, 19 } },
      info = {
        { 248, 8, 832, 3, 8 }, { 242, 0, 832, 13, 8 }, { 224, 120, 848, 17, 8 }, { 128, 56, 848, 22, 8 },
        { 224, 112, 848, 26, 8 }, { 104, 56, 848, 19, 8 }, { 170, 76, 864, 10, 10 }, { 144, 0, 864, 16, 9 },
        { 160, 0, 864, 18, 9 }, { 32, 9, 832, 5, 7 }, clut_y = 223, page = 0xC,
        ids = { colon = 0, number = 1, map = 2, save = 3, clear = 4, time = 5, percent = 6, hp = 7, mp = 8, dot = 9 } },
      digits = { clut_x = 832, clut_y = 223, w = 6, h = 10, step = 5, page = 0x0C },
      hp_digits = { u = 64, cell = 8, w = 7, h = 12, step = 6 },
      panel = { near = { 0x6B4100, 0x6C8219 }, far = { 0x330500, 0x262801 }, lit = { 0x663040, 0x203840 },
        dark = { 0x200808, 0x081010 }, mode = 0xE1000200, shadow = 0x60000000, code = 0x38000000,
        text_x = 6, top = 72, rows = 0x51 },
      gauge = { hp = 0xCFE02D, mp = 0x9F78DC, full = { 255, 240, 158 }, back = 0x60402800, mode = 0xE1000200,
        code = 0x38000000, w = 66, h = 5 },
      picture = { clut_x = 768, clut_y = 227, clut_w = 256, w = 64, h = 32, u = 0, v = 0, page = 832, slots = 48,
        special = 64 },
      clear_badge = { 240, 16, 912, 223, 16, 16, page = 0x0C },
      frame = { band = { { 256, 176, 64, 64, 0x9C }, { 0, 176, 256, 64, 0x9A }, v = 176, clut_x = 768, clut_y = 227 },
        rows = { top = 187, w = { 256, 64 } }, caution = { 10, 181, 64, 145, 848, 33, 7 },
        message = { tile = 0x60000000, y = 187, h = 52, x = 16, text_y = 192 },
        hand = { u = 32, v = 48, clut_x = 896, w = 16, h = 16,
          saturation = { 0, -56, -112, -104, -96, -88, -80, -72, -64, -56, -48, -40, -32, -24, -16, -8 } },
        fade = { mode = 0xE1000140, code = 0x62000000, step = 0x080808, w = 320, h = 224 } },
      progress = { mode = 0xE1000220, code = 0x3A000000, tile = 0x62000000, load = 0x00FFE0, save = 0x7F00FF,
        edge = 48, left = 64, h = 32, step = 16 },
    },
    -- MENUF's screens (3B8.c: the congratulations, the time attack's start and end, the cube puzzle's start, end and
    -- quit), drawn from the record's calls to the game's leaves (each with its picture's texels, page and CLUT), in
    -- `layer` (vs_scratch.unk4 - 28; the prompt's words in unk8): a picture fading in (`fade`: `steps` steps of
    -- `unit`, `grey` after; the page's blending `semi` and the next CLUT while it fades), tinted (two colours by
    -- the step), popping in (`pop`: the flash greys at steps 8 and 9, then three steps from `toward` to the
    -- colours; the dim CLUT below step `settle`), wiped in (`wipe`: `strip` wide strips, lit over `edge` pixels
    -- behind the wipe), the stat wheel's lines (`wheel`: faded by `step` a line outside `top` .. `bottom`, a
    -- line drawn twice there, added then subtracted, by the faded CLUT; else the CLUT `clut`) and its two draw
    -- areas, and the PRESS BUTTON prompt (`prompt`: sliding in `steps` steps of `step` from `dx`, its words
    -- `text_dx` right of it, `line` apart, in `colour`; the circle button bobbing by the blink timer). `place`
    -- moves the pictures and the prompt (not the draw areas) from the game's places.
    menuf = {
      place = { dx = 0, dy = 0 }, layer = -28, semi = 0x20, subtract = 0x40,
      fade = { steps = 8, unit = 16, grey = 128 },
      pop = { flash = { [8] = 192, [9] = 224 }, settle = 14, dim = 10, toward = 192 },
      wipe = { strip = 12, edge = 64 },
      wheel = { top = 190, bottom = 206, step = 16, grey = 128, faded_clut = { 816, 511 }, clut = { 800, 511 } },
      prompt = { dx = 288, step = 6, steps = 48, text_dx = 18, line = 10, colour = 0x8080FF, drop = 22,
        button = "circle", w = 16, h = 16, top = -2 },
    },
    -- The buttons' 16 x 16 sprites (vs_mainmenu_renderButton: texels at u by the button, v 128; the CLUT at 944).
    buttons = { u = { triangle = 0, square = 16, circle = 32, cross = 48 },
      sprite = { page_x = 768, page_y = 0, bits = 4, u = 0, v = 128, w = 16, h = 16, clut_x = 944, clut_y = 223,
        bgr = 0x808080 } },
    -- Storage's (MENUD) button strip, sliding in by x: two button windows (`windows`: x offset, y, w, h; in
    -- `window_layer`), the buttons (`buttons`: which, x offset, y) and the words (`words`: text, x offset, y, colour
    -- and how the state lights it: STATUS `colour << (state & 3)`, END `colour >> (state >> 2)`).
    storage_buttons = {
      place = { anchor = "game", dx = 0, dy = 0 }, window_layer = -12,
      windows = { { 0, 0x26, 0x90, 0xA }, { 0, 0x36, 0x48, 0xA } },
      buttons = { { "square", -8, 0x24 }, { "triangle", 0x40, 0x24 }, { "cross", -8, 0x34 } },
      words = { { "STATUS", 12, 0x26, 0x202020, "lit" }, { "CANCEL", 12 + 0x48, 0x26, 0x808080 },
        { "END", 12, 0x36, 0x808080, "dim" } },
    },
    -- Storage's counts (func_80106464: "capacity" right-aligned at x, "/" 32 left, the count 32 left of it, `low`
    -- when under half full). The strip's (layout 0): the bag's or the container's name at (x, name_y), each
    -- category's name (`categories`, from x + name_x) and its counts (x + count_x), `line` apart from `top`, the
    -- last row the transfer list's. The top menu's (layout 1): `heads` at `head_x`, `head_y`; per category the bag's
    -- counts (column 1), the name centred (`$`) at `name_x` and the container's counts (column 2), from `top`.
    storage_counts = {
      place = { anchor = "game", dx = 0, dy = 0 }, colour = 0x808080, low = 0x804020, slash = "/", slash_dx = -0x20,
      strip = { names = { [0] = "GADGET BAG", "CONTAINER" }, name_y = 0x4C, top = 0x58, line = 12, name_x = 8,
        count_x = 0x70, categories = { "WEAPON", "BLADE", "GRIP", "SHIELD", "ARMOR", "GEM", "MISC" } },
      summary = { heads = { "GADGET BAG", "CONTAINER" }, head_x = { 0x3A, 0xBA }, head_y = 0x58, top = 0x64, line = 12,
        columns = { 0x70, 0xF8 }, name_x = 0xA0,
        categories = { "$WEAPON", "$BLADE", "$GRIP", "$SHIELD", "$ARMOR", "$GEM", "$MISC" } },
    },
    -- Loot's (MENUB) capacities strip, sliding in by x: GADGET BAG, then per category (`line` apart from `top`) its
    -- name, "/" and the bag's capacity and count, grey (`colour`), dimmed (`dim`, lit `<< 1`) for the categories the
    -- row looked at does not touch; a count under half its capacity `low`, one the item would overflow `over`. Then
    -- the windows and the GET / ALL / END buttons and words (lit by the controls; ALL by ALL's state too).
    loot_capacities = {
      place = { anchor = "game", dx = 0, dy = 0 }, colour = 0x808080, dim = 0x404040, low = 0x804020,
      over = 0x204080, window_layer = -12, head = { "GADGET BAG", 176, 12 }, top = 24, line = 12,
      name_x = 192, slash_x = 272, capacity_x = 296, count_x = 272, slash = "/",
      categories = { "WEAPON", "BLADE", "GRIP", "SHIELD", "ARMOR", "GEM", "MISC" },
      windows = { { 184, 112, 112, 10 }, { 184, 128, 64, 10 } },
      words = { { "GET", 196, 112, "circle", 176, 110 }, { "ALL", 252, 112, "square", 232, 110, "all" },
        { "END", 196, 128, "cross", 176, 126 } },
    },
    -- How ui_text sets both (with ui_text font; the packets stay the game's): the status style,
    -- typeset as one word centred where the game centres it.
    typeset = {
      style = "status",
      blanks = "one",        -- a run of the game's narrow blanks is one space ("each": a space for each)
      keep_on_screen = true, -- moved only as far as it takes to stay on the screen (WELL TIMED! starts at x 5)
      screen_w = 320,        -- the field's screen (16:9 anchors the HUD at the window's edges: the same holds)
      margin = S("ui_text_edge_margin"),   -- pixels beyond the outline kept from the edges
      word_space = S("ui_text_word_space"), -- the font's space times this
    },
    -- ui_text's styles for this run (rood.hud.typography: font, size, stroke, tracking, stretch, slant,
    -- embolden, ...; over the ui_text_<style>_* settings; empty: the settings'), and its tunables:
    typography = {
      status = {},
      -- the gauges' digits: the font's old-style figures (3 4 5 7 9 descend) rise together into the room
      -- above them, by this much of what their deepest descender lacks, before any is squeezed (playtest fix
      -- 1d2f20c5: 1, all it takes; 0 the old squeeze that flattened 5s and 7s)
      digits = { descender_lift = S("ui_text_descender_lift") },
    },
    -- The deliberate differences from the game's packets, per kind (the parity gate's waivers):
    -- typography changes no packet (ui_text restyles at raster time); a place with dx, dy or another
    -- anchor would be "place".
    -- (the buffs' and the numbers' digits are the digits style's: look.typography.digits sets them too)
    overrides = { status = { "typography" }, chain = { "typography" }, gauges = { "typography" },
      buff = { "typography" }, damage = { "typography" } },
  }
end

local SETTINGS = { "ui_text_edge_margin", "ui_text_word_space", "ui_text_descender_lift" }
local look
local chain_text = {} -- "$3 CHAINS" by count, made once (no string per frame)

local function refresh()
  look = make_look()
  M.look = look
  chain_text = {}
  for style, fields in pairs(look.typography) do
    rood.hud.typography(style, next(fields) and fields or nil)
  end
end
M.refresh = refresh
refresh()
for _, key in ipairs(SETTINGS) do rood.settings.on_change(key, refresh) end

-- x moved by a slide table: in at ages 0..3, out from `out` on (the game's steps backwards).
local function slid(x, age, slide, out, coming_in)
  if coming_in and age < 4 then return x - slide[age + 1] end
  if age >= out then
    local step = out + 3 - age
    if step >= 0 and step < 4 then return x - slide[step + 1] end
  end
  return x
end

-- status: func_800958A4 (x, y: its call's 44, 48)
M.draw.status = function(e, d)
  local s = look.status
  local text = e.timing == "success" and s.success[(e.variant or 0) + 1] or s.words[e.timing]
  if not text then return end
  d.anchor(s.place.anchor)
  local x = slid(e.call_x, e.age, s.slide, s.slide_out, true)
  d.text_raw(text, x + s.place.dx, e.call_y + s.place.dy, s.colours[e.timing], look.typeset)
end

-- chain: func_80095A4C (x, y: 44, 64)
M.draw.chain = function(e, d)
  local c = look.chain
  local text = chain_text[e.value]
  if not text then
    text = e.value == 1 and c.one or string.format(c.many, e.value)
    chain_text[e.value] = text
  end
  d.anchor(c.place.anchor)
  local x = slid(e.call_x, e.age, c.slide, c.slide_out, e.value == 1)
  d.text_raw(text, x + c.place.dx, e.call_y + c.place.dy, c.colour, look.typeset)
end

-- gauges: func_800CA2DC's _renderStatBar(0, 1, 2) and RISK ZERO. The game links each packet at the head of
-- its slot, so a slot draws in the reverse of the game's calls: the commands below go in drawing order
-- (RISK ZERO, then RISK, MP, HP: each its label, value, separator, maximum, bar).
local xy = rood.draw.xy
local function digits(v)
  local n = 1
  while v >= 10 do v, n = v // 10, n + 1 end
  return n
end
local function gauge(d, g, i, current, max, dy)
  local p = g.pools[i]
  if max == 0 then return end -- (the game draws nothing for a pool with no maximum)
  local bx, by = xy(p.bar[1] + g.place.dx, p.bar[2] + g.place.dy, dy << 16)
  local mx, my, sx, sy, cx, cy
  if i == 3 then
    cx, cy = xy(bx, by, g.risk_value)
  else
    mx, my = xy(bx, by, g.maximum)
    sx, sy = xy(mx, my, -digits(max) * g.digit_step) -- (the pen the small digits leave)
    cx, cy = xy(sx, sy, g.separator)
  end
  local lx, ly = xy(p.label[1] + g.place.dx, p.label[2] + g.place.dy, dy << 16)
  d.layer(g.layers.bars)
  d.sprite(lx, ly, p.sprite)
  d.layer(g.layers.digits)
  d.value(1, cx, cy, current)
  if i ~= 3 then
    d.layer(g.layers.bars)
    d.digit(2, sx, sy, 0)
    d.layer(g.layers.digits)
    d.value(0, mx, my, max)
  end
  d.layer(g.layers.bars)
  d.bar(bx, by, current, max, p.pool)
end
M.draw.gauges = function(e, d)
  local g = look.gauges
  local dy = -(e.slide >> 16) -- (the display slides up 8 pixels a step)
  d.anchor(g.place.anchor)
  if e.risk == 0 then
    d.layer(g.layers.bars)
    d.text_raw(g.zero.text, g.zero.x + g.place.dx, g.zero.y + g.place.dy + dy, g.zero.colour)
  else
    gauge(d, g, 3, e.risk, g.risk_max, dy)
  end
  gauge(d, g, 2, e.mp, e.max_mp, dy)
  gauge(d, g, 1, e.hp, e.max_hp, dy)
end

-- Commands are in drawing order: the game's last limb is at the head of the slot.
local limb_fields = { "limb_0_status", "limb_1_status", "limb_2_status", "limb_3_status", "limb_4_status" }
M.draw.figure = function(e, d)
  local f = look.figure
  d.anchor(f.place.anchor)
  d.layer(f.layer)
  for i = 5, 1, -1 do
    local p = f.stances[e.stance + 1][i]
    local s = f.sprite -- reused descriptor: draw copies its values, no allocation each frame
    s.w, s.h, s.u, s.v, s.bgr = p[3], p[4], p[5], p[6], f.colours[e[limb_fields[i]] + 1]
    local x, y = xy(f.place.x + f.place.dx, f.place.y + f.place.dy, p[1] | (p[2] << 16))
    d.sprite(x, y, s)
  end
end

-- Drawing order, not allocation order: reverse the game's calls within each OT slot.
local function status_sprite(d, s, x, y, w, h, uv, clut, set_page)
  local p = s.sprite
  p.set_page = set_page ~= false
  p.w, p.h, p.u, p.v = w, h, uv & 255, (uv >> 8) & 255
  p.clut_x, p.clut_y = (clut & 63) * 16, clut >> 6
  d.sprite(x + s.place.dx, y + s.place.dy, p)
end
local function status_cell(d, s, id, offset, side)
  if id == 255 then return end
  local uv, x, w = s.texels[id + 1], s.x + side * s.cell, s.cell
  if offset < 0 then uv, w = uv - offset, w + offset
  else x, w = x + offset, w - offset end
  local clut = s.cluts[((s.alternate_clut >> id) & 1) + 1]
  -- A clipped buff/debuff also has a stat glyph; emit it first (C links it last).
  local width, alignment = s.supplemental_w, s.supplemental_alignment
  if (id & 16) ~= 0 and (x & alignment) == 0 then
    local sx, sw = x, w
    local suv = (id & 3) * s.supplemental_stride + s.supplemental_uv
    if sw > width then
      if (sx & (alignment - 1)) ~= 0 then sx, sw = sx + width, sw - width
      else sx, sw = sx - width + sw, width end
    else suv = suv + width - sw end
    status_sprite(d, s, sx, s.y, sw, s.supplemental_h, suv, s.supplemental_clut)
  end
  status_sprite(d, s, x, s.y, w, s.cell, uv, clut)
end
M.draw.status_icons = function(e, d)
  local s, o = look.status_icons, e.offset
  d.anchor(s.place.anchor)
  d.layer(s.layers.icons)
  if e.slide == 0 then
    status_cell(d, s, e.previous, o, 0)
    status_cell(d, s, e.current, o + s.cell, 0)
    status_cell(d, s, e.current, o, 1)
    status_cell(d, s, e.next, o + s.cell, 1)
  end
  d.layer(s.layers.indicators)
  local p = s.indicator
  local x, y = xy(p.x, p.y, -e.slide)
  if e.count < p.strip_from then
    for i = e.count - 1, 0, -1 do
      status_sprite(d, s, x + i * p.step, y, p.size, p.size, p.dot_uv, p.clut)
    end
  else
    status_sprite(d, s, x, y, math.min(e.count, p.columns) * p.step + p.strip_padding, p.size, p.strip_uv, p.clut)
    if e.count > p.columns then
      status_sprite(d, s, x, y + p.row_step, (e.count - p.columns) * p.step + p.strip_padding, p.size, p.strip_uv, p.clut, false)
    end
    if e.selected ~= 255 then
      status_sprite(d, s, x + (e.selected & (p.columns - 1)) * p.step,
        y + ((e.selected & p.columns) == 0 and 0 or p.row_step), p.size, p.size, p.dot_uv, p.clut, false)
    end
  end
end

M.draw.battle_mode = function(e, d)
  local b = look.battle_mode
  d.anchor(b.place.anchor)
  d.layer(b.layer)
  -- The game adds sine>>5 to the packed BGR word, NOT to every channel.
  b.sprite.bgr = (b.colour + e.pulse) & 0xFFFFFF
  local x, y = xy(b.x, b.y + e.step * b.step_y, -e.slide)
  d.sprite(x + b.place.dx, y + b.place.dy, b.sprite, nil, b.alpha)
end

local function ability_sprite(d, a, sprite, x, y, w, h, u, v, colour, alpha)
  sprite.w, sprite.h, sprite.u, sprite.v, sprite.bgr = w, h, u, v, colour
  d.sprite(x + a.place.dx, y + a.place.dy, sprite, nil, alpha)
end
M.draw.ability_icons = function(e, d)
  local a, id, offset = look.ability_icons, e.group, e.offset
  d.anchor(a.place.anchor)
  d.layer(a.layer)
  if id == 8 then
    local c = a.cross
    local x, y = xy(c.x + offset, c.y, 0) -- C masks x; no carry into y
    ability_sprite(d, a, a.button_sprite, x, y, c.w, c.h, c.u, c.v, a.full_colour, a.semi_alpha)
    return
  end
  local xbase = id < 4 and a.right_x - offset or offset
  if id < 4 then
    local f, index = a.face, 4 - id
    local x, y = xy(0, 0, a.face_xy[index] + xbase) -- unlike labels, packed carry is intentional
    ability_sprite(d, a, a.button_sprite, x, y, f.size, f.size, (index - 1) * f.u_step, f.v, a.full_colour, 1)
  end
  if e.availability ~= 0 then
    local p, m = a.labels[id + 1], a.marker
    local style = a.availability[e.availability]
    local colour, alpha = style.colour, style.alpha
    local x, y = xy(xbase + p[3], p[4], 0)
    ability_sprite(d, a, a.label_sprite, x, y, p[5], p[6], p[7], p[8], colour, alpha)
    x, y = xy(xbase + p[1], p[2], 0)
    ability_sprite(d, a, a.label_sprite, x, y, m.w, m.h, m.u, m.v, colour, alpha)
  end
end

-- buff: func_8009695C (x, y: its call's 8, 172). The game links each quad at the head of its slot: the commands
-- go in drawing order (the digits from the left, then the sign, the class or affinity, the equipment).
local function quad(d, q, x, y, w, h, u, v, tw, th, bgr)
  q.u, q.v, q.w, q.h, q.bgr = u, v, tw, th, bgr -- (descriptors reused: no table per frame)
  d.quad(x, y, w, h, q)
end
M.draw.buff = function(e, d)
  local b = look.buff
  local slot, stat = b.slots[e.slot], b.stats[e.stat]
  if not slot or not stat then return end
  local colour = e.sign > 0 and b.colours.gain or b.colours.loss
  local x, y = e.call_x + b.place.dx, e.call_y - e.rise + b.place.dy
  local stat_x = x + slot[3] + b.gap
  local sign_x = stat_x + stat[3] + b.gap
  local s, f = b.sign, b.digits
  local n = digits(e.value)
  local right = sign_x + s.after + n * f.step -- (the rightmost digit's x)
  local div = 1
  for _ = 2, n do div = div * 10 end
  d.anchor(b.place.anchor)
  d.layer(b.layer)
  for i = n - 1, 0, -1 do
    local digit = e.value // div % 10
    div = div // 10
    quad(d, b.figures, right - i * f.step, y + f.dy, f.w, f.h, digit * f.u_step, f.v, f.w, f.h, colour)
  end
  quad(d, b.words, sign_x, y + s.dy, s.w, s.h, e.sign > 0 and s.gain_u or s.loss_u, s.v, s.w, s.h, colour)
  quad(d, b.words, stat_x, y, stat[3], stat[4], stat[1], stat[2], stat[3], stat[4], colour)
  quad(d, b.words, x, y, slot[3], slot[4], slot[1], slot[2], slot[3], slot[4], colour)
end

-- damage: func_80094B0C (x, y: where the label loop placed it, its record's spread applied). The digits from
-- the left (the game's last linked), each off the screen left out as the game leaves it; a stat's sign and
-- icon after them.
M.draw.damage = function(e, d)
  local m, f = look.damage, look.damage.digits
  local gain = e.sign > 0
  local x, y = e.call_x + m.place.dx, e.call_y + m.place.dy
  local n, offset, colour, width = digits(e.value), 0, 0, 0
  local st = e.pool == "stat" and m.stat or nil
  local icon = st and st.icons[e.stat + 1]
  if st then
    if not icon then return end
    y = y + (gain and -(e.age >> 1) or e.age >> 1) -- (its drift)
    width = (n * f.step + icon[3] + st.padding) // 2
    colour = m.colours.stat.gain
  else
    local pool = m.colours[e.pool]
    if not pool then return end
    local frame = e.age < m.last_frame + 1 and e.age or m.last_frame
    offset = m.bounce[frame + (gain and 0 or m.loss_frame) + 1]
    colour = gain and pool.gain or pool.loss
    width = n * f.centre
  end
  local right = x + width - f.step -- (the rightmost digit's x)
  local div = 1
  for _ = 2, n do div = div * 10 end
  d.anchor(m.place.anchor)
  d.layer(m.layer)
  for i = n - 1, 0, -1 do -- (i: the digits to its right; each halves the bounce, towards zero)
    local digit = e.value // div % 10
    div = div // 10
    local dx = right - i * f.step
    if dx + f.w >= 0 and dx + f.w < f.clip then
      local bounce = offset >= 0 and offset >> i or -((-offset) >> i)
      quad(d, m.figures, dx, y + bounce - f.half, f.w, f.h, digit * f.u_step + f.u, f.v, f.w, f.h, colour)
    end
  end
  if st then
    local s, ix = st.sign, x - width
    quad(d, s, ix + icon[3] + s.gap, y + s.dy, s.w, s.h, gain and s.gain_u or s.loss_u, s.v, s.w, s.h)
    quad(d, st.icon, ix, y - st.half, icon[3], st.h, icon[1], icon[2], icon[3], st.h)
  end
end

-- actor_word: func_8009506C (MISS) or func_80095258 (a combat label) at the label loop's place. The game links
-- each piece at the head of the slot: the commands go last piece first (a label's arrow, linked before its
-- pieces, after them).
local function word_quad(d, q, x, y, w, h, u, v, th, bgr, alpha)
  q.u, q.v, q.w, q.h, q.bgr = u, v, w, th, bgr
  d.quad(x, y, w, h, q, alpha)
end
local function miss(e, d, a)
  local m = a.miss
  local step = e.age < a.enter and e.age or e.age >= a.leave and a.life - e.age or a.enter
  local x = e.call_x + m.dx + a.place.dx
  local y0, y1 = e.call_y + a.place.dy - m.squash[step + 1], e.call_y + a.place.dy + m.squash[step + 1]
  local right = x
  for i = 1, #m.letters do right = right + m.letters[i][3] end
  for i = #m.letters, 1, -1 do
    local l = m.letters[i]
    right = right - l[3]
    if right < 320 and right + l[3] >= 0 then
      word_quad(d, m.sprite, right, y0, l[3], y1 - y0, l[1], l[2], m.h, nil, m.alpha)
    end
  end
end
local function channel(c, step, a)
  if step == 5 then return c // 2 + a.lit end
  if step == 4 then return c // 2 end
  return c * a.fade[step + 1] // 32
end
local function label(e, d, a)
  local entry = a.labels[e.label + 1]
  if not entry or #entry < 2 then return end
  local x, y = e.call_x + a.place.dx, e.call_y + a.place.dy
  local age, step = e.age, 4
  if age < a.enter then
    step = age
    if not e.lowered then x = x + a.slide[step + 1] end
  elseif age < a.lit_until then
    step = 5
  elseif age >= a.leave then
    step = a.life - age
    if not e.lowered then x = x - a.slide[step + 1] end
  elseif age >= a.lit_from then
    step = 5
  end
  local arrow_x = x
  local width = 0
  for i = 2, #entry do width = width + a.pieces[entry[i] + 1][3] end
  x = x - width // 2
  local c = a.colours[(e.lowered and 0 or entry[1]) + 1]
  local bgr = channel(c[1], step, a) | channel(c[2], step, a) << 8 | channel(c[3], step, a) << 16
  local q = step ~= 4 and a.fading or a.held
  local right = x + width
  for i = #entry, 2, -1 do
    local r = a.pieces[entry[i] + 1]
    right = right - r[3]
    if right < 320 and right + r[3] >= 0 then
      word_quad(d, q, right, y - a.half, r[3], a.h, r[1], r[2], a.h, bgr, a.alpha)
    end
  end
  local w = a.arrow
  if e.lowered and age >= w.from then
    local cell = math.min(age - w.from, w.steps)
    word_quad(d, w.sprite, arrow_x + w.dx, y + w.dy, w.w, w.h, cell * w.u_step + w.u, w.v, w.h)
  end
end
M.draw.actor_word = function(e, d)
  local a = look.actor_word
  d.anchor(a.place.anchor)
  d.layer(a.layer)
  if e.word == "miss" then miss(e, d, a) else label(e, d, a) end
end

-- timer: _renderTimer's vs_battle_renderTextRaw (its string, as the game composed it: e.text).
M.draw.timer = function(e, d)
  local t = look.timer
  d.anchor(t.place.anchor)
  d.layer(t.layer)
  d.text_raw(e.text, t.x + t.place.dx, t.y + t.place.dy, t.colour)
end

-- emote: func_80095C18 as it decided (e.draw). The game links each piece at the head of the slot: the balloon
-- (linked last) first, then its marks in the reverse of the game's calls.
local function emote_balloon(d, m, e)
  local q = m.balloon
  q.corners, q.uvs = m.corners[e.side + 1], m.uvs[e.side + 1] -- (reused: no table per frame)
  d.quad(e.call_x + m.place.dx, e.call_y + m.place.dy, 0, 0, q)
end
local function emote_sprite(d, m, e, delta, sprite)
  local x, y = xy(e.call_x + m.place.dx, e.call_y + m.place.dy, delta)
  d.sprite(x, y, sprite)
end
local ARROW_TURNS = { 0, 2, 6, 4 } -- (the arrow's vertices 0, 1, 3, 2 are its points turned by two each)
M.draw.emote = function(e, d)
  local m = look.emote
  d.anchor(m.place.anchor)
  if e.draw == "arrow" then
    local a = m.arrow
    local i, c = a.directions[e.direction], a.quad.corners
    if not i then return end
    for k, step in ipairs(ARROW_TURNS) do
      local at = (i + step) % 8 + 1
      c[2 * k - 1], c[2 * k] = a.dx[at], a.dy[at]
    end
    d.quad(e.call_x + m.place.dx, e.call_y + m.place.dy, 0, 0, a.quad)
    return
  end
  local kind, mark = e.emote, m.marks[e.side + 1]
  if kind == 7 then
    emote_sprite(d, m, e, m.question.offset, m.question.sprite)
    return
  end
  emote_balloon(d, m, e)
  if kind >= 1 and kind <= 3 then
    for k = 0, kind - 1 do emote_sprite(d, m, e, k * m.dot.step + mark, m.dot.sprite) end
  elseif kind == 4 then
    emote_sprite(d, m, e, mark, m.shout)
  elseif kind == 5 then
    emote_sprite(d, m, e, mark + m.heart.offset, m.heart.sprite)
  end
end

-- text_frame: func_800CD3E4 for the box as the game set it this frame.
local frame = {}
M.draw.text_frame = function(e, d)
  local t = look.text_frame
  frame.box, frame.shape, frame.flip, frame.tail = e.box, e.shape, e.flip, e.tail
  d.anchor(t.place.anchor)
  d.frame(e.box_x + t.place.dx, e.box_y + t.place.dy, e.box_w, e.box_h, frame)
end

-- text_band: func_800CD158 for the box as the game set it this frame.
local band = {}
M.draw.text_band = function(e, d)
  band.top, band.edge, band.lines, band.settled, band.icon = e.top, e.edge, e.lines, e.settled, e.icon
  d.anchor(look.text_band.place.anchor)
  d.band(e.box, band)
end

-- box_text: e.glyphs, 6 integers a glyph (code, font table, style, x, y, brightness) in the order the printer set
-- them; each links at the head of the box's entry, so the commands go last glyph first.
M.draw.box_text = function(e, d)
  local p, g = look.box_text.place, e.glyphs
  d.anchor(p.anchor)
  for i = #g - 5, 1, -6 do
    d.char(g[i], g[i + 3] + p.dx, g[i + 4] + p.dy, g[i + 1], g[i + 2], g[i + 5])
  end
end

M.draw.image = function(e, d)
  d.anchor(look.image.place.anchor)
  d.image(e.image, e.drawer, e.clut)
end

-- target_row: func_800C1DC4's name and plate for the row as the game read it (its fade before the game steps it). The
-- game links the plate after the name at the head of the row's slot: the plate's command first.
local function lerp(c0, c1, brightness, f) -- (_lerpColor: each of a colour word's three bytes)
  local out = 0
  for shift = 16, 0, -8 do
    out = (out << 8) + ((((c0 >> shift) & 255) * (8 - f) + ((c1 >> shift) & 255) * f) * brightness >> 10)
  end
  return out
end
M.draw.target_row = function(e, d)
  local r = look.target_row
  local p, n, plate, sh = r.place, r.name, r.plate, r.shadow
  local step, bank = e.selected and plate.steps or e.fade, e.bank * plate.steps
  if bank > plate.steps or step < 0 or step > plate.steps then return end
  local near = lerp(r.near[1], r.near[2], plate.brightness, bank)
  local far = lerp(r.far[1], r.far[2], plate.brightness, bank)
  local left_c, right_c = lerp(near, far, plate.brightness, plate.steps - step), lerp(near, far, plate.brightness, step)
  local left = e.call_x < r.half
  if left then left_c, right_c = right_c, left_c end
  local x, y = e.call_x + p.dx, e.call_y + p.dy
  local wider = left and sh.wider or 0
  if p.anchor == "edges" then d.anchor(widescreen() and (left and "left" or "right") or "game") else d.anchor(p.anchor) end
  d.plate(x, y, plate.w, plate.h, left_c, right_c, x + sh.dx - wider, y + sh.dy, plate.w + wider, plate.h)
  d.text_raw(e.text, x + (left and n.left or n.right), y + n.dy, e.selected and n.selected or n.colour)
end

-- target_line: func_800C1DC4's leader lines for the selected row. Each links at the head of vs_scratch.unk8: the
-- second (from `trail` on) is drawn first, so its command goes first.
M.draw.target_line = function(e, d)
  local l = look.target_line
  local p, r = l.place, l.ring
  local left = e.row_x < l.half
  local x0, y0 = e.call_x + p.dx, e.call_y + p.dy
  local x1, y1 = e.row_x + (left and r.left or r.right) + p.dx, e.row_y + r.dy + p.dy
  if p.anchor == "edges" then
    if widescreen() then x1 = x1 + (left and -l.edge_shift or l.edge_shift) end
    d.anchor("game")
  else
    d.anchor(p.anchor)
  end
  if e.age >= l.trail then d.leader(x0, y0, x1, y1, (e.age + l.step) % l.cycle, left) end
  d.leader(x0, y0, x1, y1, e.age - l.lag, left)
end

-- target_marker: func_800C28AC's sprites and brackets. Each links at the head of the slot: the last sprite's command
-- first, the brackets' last.
M.draw.target_marker = function(e, d)
  local m = look.target_marker
  local x, y = e.call_x + m.place.dx, e.call_y + m.place.dy
  local first = e.lock and m.locked or 0
  d.anchor(m.place.anchor)
  d.layer(m.layer)
  local s = m.sprite
  for i = 2, 0, -1 do
    local k = i + first + 1
    local t = m.sprites[k]
    local sx, sy = xy(x, y, t[1])
    s.w, s.h, s.u, s.v = t[2], t[3], t[4], t[5]
    s.bgr = (k == m.pulse_at and m.pulse[e.step + 1] or m.brightness) * 0x010101
    d.sprite(sx, sy, s)
  end
  if e.lock then return end
  local q = m.bracket
  for b = #m.brackets, 1, -1 do
    local br = m.brackets[b]
    for c = 1, 4 do
      local cx, cy = xy(x, y, br.deltas[c])
      q.corners[2 * c - 1], q.corners[2 * c] = cx - x, cy - y
    end
    q.uvs = br.uvs
    d.quad(x, y, 0, 0, q)
  end
end

-- MAINMENU's shared frame (58EC.c). A row: the game's own emit on a copy of the row (d.menu_row).
M.draw.menu_row = function(e, d)
  local p = look.menu_row.place
  d.anchor(p.anchor)
  if p.dx ~= 0 or p.dy ~= 0 then
    d.menu_row({ x = e.row.x + p.dx, y = e.row.y + p.dy })
  else
    d.menu_row()
  end
end

-- menu_cursor: vs_mainMenu_renderCursor's sprite (setSpriteDefaultTexPage in vs_scratch.unk8).
M.draw.menu_cursor = function(e, d)
  local c = look.menu_cursor
  local s = c.sprite
  d.anchor(c.place.anchor)
  d.layer("unk8")
  s.clut_x = c.cluts[e.color + 1] or c.cluts[1]
  s.bgr = c.pulse[e.step + 1] * 0x010101
  d.sprite(e.call_x + c.place.dx, e.call_y + c.place.dy, s)
end

-- menu_command: _renderMenuCommand's sprite (setSprite 0x180: semi-transparent).
M.draw.menu_command = function(e, d)
  local c = look.menu_command
  local size, uv = c.sizes[e.action], c.uvs[e.action]
  if not size then return end
  local s = c.sprite
  d.anchor(c.place.anchor)
  d.layer("unk8")
  s.w, s.h, s.u, s.v = size[1], size[2], uv[1], uv[2]
  d.sprite(c.right - size[1] + c.place.dx, e.call_y + c.top + c.place.dy, s, nil, 0.5)
end

-- menu_tabs: vs_mainMenu_renderTabNavigation (in drawing order: the right arrow, the left, then the labels last
-- to first; each links at the head of vs_scratch.unk8).
M.draw.menu_tabs = function(e, d)
  local t = look.menu_tabs
  local s, at, bob = t.sprite, t.arrows[e.mode ~= 0 and 1 or 0], e.step // 4
  local dx, dy = t.place.dx, t.place.dy
  d.anchor(t.place.anchor)
  d.layer("unk8")
  s.clut_x = t.cluts[e.lit]
  s.u = 16
  d.sprite(at[2][1] + bob + dx, at[2][2] + dy, s)
  s.u = 0
  d.sprite(at[1][1] - bob + dx, at[1][2] + dy, s)
  if e.mode ~= 0 then
    for i = #t.labels, 1, -1 do
      local l = t.labels[i]
      d.text_raw(l[1], l[2] + dx, l[3] + dy, t.colours[e.lit])
    end
  end
end

-- menu_background: _renderBackground's sprites (in drawing order: the frame behind, then the picture).
M.draw.menu_background = function(e, d)
  local b = look.menu_background
  local dx, dy = b.place.dx, b.place.dy
  local opaque = e.fade == 16
  local level = opaque and e.brightness or e.fade * 8
  d.anchor(b.place.anchor)
  if not opaque then
    local h, u = b.behind, e.buffer == 0 and 320 or 0
    h.bgr = (128 - level) * 0x010101
    h.page_x, h.w = u + 256, 64
    d.sprite(256 + dx, dy, h)
    h.page_x, h.w = u, 256
    d.sprite(dx, dy, h)
  end
  local s = b.sprite
  s.bgr = (level % 256) * 0x010101
  for i = #b.picture, 1, -1 do
    local p = b.picture[i]
    s.page_x, s.w = p[3], p[2]
    d.sprite(p[1] + dx, dy, s, nil, (opaque and level < 256) and 1 or 0.5)
  end
end

-- option_value: classic_options.c draw_values' row, the same printer calls from the same copied inputs. Each glyph
-- links at the head of its slot, so the commands go in the reverse of the page's calls (places worked out first).
M.draw.option_value = function(e, d)
  local o, l = e.option, look.option_value
  local dx, dy = l.place.dx, l.place.dy
  local x, y, right, up = e.call_x + dx, e.call_y + dy, e.option.right + dx, { upper = e.option.upper }
  local k = o.kind
  d.anchor(l.place.anchor)
  if k == "toggle" then
    local off_x = x + d.option_width(o.on, up) + o.gap
    d.option_text(o.off, off_x, y, o.index == 0 and o.lit or o.other, up)
    d.option_text(o.on, x, y, o.index == 1 and o.lit or o.other, up)
  elseif k == "choice" then
    local arrow = d.option_width("<", up) + l.arrow_gap
    local room = right - x - 2 * arrow
    local pen = x + arrow + d.option_width(o.value, { room = room, upper = o.upper })
    if o.focused then d.option_text(">", pen + l.arrow_gap, y, 0, up) end
    d.option_text(o.value, x + arrow, y, o.lit, { room = room, upper = o.upper })
    if o.focused then d.option_text("<", x, y, 0, up) end
  elseif k == "slider" then
    local tw = d.option_width(o.value, up)
    d.option_text(o.value, tw < right - x and right - tw or x, y, o.lit, { room = right - x, upper = o.upper })
    if x + o.bar_width + l.bar_room + tw <= right then d.option_bar(x, y, o.fill, o.bar_colour) end
  elseif k == "binding" or k == "info" then
    d.option_text(o.value, x, y, o.enabled and 0 or 3, { room = right - x, upper = o.upper })
  elseif k == "link" then
    d.option_text(">", right - d.option_width(">", up), y, o.link and 0 or 3, up)
  elseif k == "note" then
    local wx = o.window_x + dx
    d.option_text(o.label, wx + l.note_x, y, 3, { room = right - wx - l.note_x, upper = false })
  end
end

-- items_category: _renderItemCategory's loop (each icon links at the head of its slot: the commands go last to first).
M.draw.items_category = function(e, d)
  local c, it = look.items_category, e.items
  local s, dx, dy = c.sprite, c.place.dx, c.place.dy
  d.anchor(c.place.anchor)
  for i = 7, 0, -1 do
    local x = ((i + 1) % 8) * c.gap + c.left
    s.u, s.v = c.u0 + i * 16, c.v
    s.clut_x = i == it.selected and c.cluts.lit or c.cluts.dim
    if i == it.selected and it.highlight then
      if it.on_row ~= i then
        d.layer("unk8")
        d.sprite(x + dx, c.raised_y + dy, s)
      end
    else
      d.layer(it.slot)
      d.sprite(x + dx, it.step + dy, s)
    end
  end
end

-- equip_stats: vs_mainMenu_renderEquipStats' calls, last to first (each links at the head of unk4 - 24).
local function threshold_colour(c, value, threshold)
  return threshold < value and c.up or value < threshold and c.down or c.colour
end
M.draw.equip_stats = function(e, d)
  local c, q = look.equip_stats, e.equip
  local x, y = q.x + c.place.dx, q.y + c.place.dy
  local known, cols = q.known ~= 0, c.columns
  d.anchor(c.place.anchor)
  d.layer(c.layer)
  local name = (q.subtype & 7) ~= 0 and 4 or 0
  for i = 2, 0, -1 do
    local row = y + c.line * (3 + i)
    if known then
      local stat = q.stat[i + 1]
      local total = stat + q.total[i + 1]
      local a3 = (q.known & 1) ~= 0 and q.accessory[i + 1] or 0
      local shown = stat + q.base[i + 1] + a3
      d.menu_int(shown, x + cols[2], row, threshold_colour(c, shown, total + a3))
      d.menu_int(stat, x + cols[1], row, threshold_colour(c, stat, q.threshold[i + 1]))
    else
      d.text_raw(c.unknown, x + cols[2], row, c.colour)
      d.text_raw(c.unknown, x + cols[1], row, c.colour)
    end
    local label = c.names[(q.subtype & 0x18) ~= 0 and 8 + name + i or name + i]
    if label then d.text_raw(label, x, row, c.colour) end
  end
  if q.range_risk then
    for i = 1, 0, -1 do
      local row = y + c.line * (1 + i)
      if known then
        local stat = i == 0 and q.range or q.risk
        d.menu_int(i == 0 and stat + q.range_delta or stat, x + cols[2], row, c.colour)
        d.menu_int(stat, x + cols[1], row, c.colour)
      else
        d.text_raw(c.unknown, x + cols[2], row, c.colour)
        d.text_raw(c.unknown, x + cols[1], row, c.colour)
      end
      d.text_raw(c.range_risk[i + 1], x, row, c.colour)
    end
    d.text_raw(c.header, x + c.header_x, y, c.colour)
  else
    d.text_raw(c.header, x + c.header_x, y + 2 * c.line, c.colour)
  end
end

-- dp_pp_bars: vs_mainMenu_renderDpPpBars' calls, last to first (unk4 - 12): the gauges, then each bar's numbers
-- (the current value at the slash's pen moved by the game's packed (-7, -2): 7 left, 1 up) and word.
local function decimal_digits(v)
  local n = 1
  while v >= 10 do v, n = v // 10, n + 1 end
  return n
end
M.draw.dp_pp_bars = function(e, d)
  local c, b = look.dp_pp_bars, e.bars
  local dx, dy = c.place.dx, c.place.dy
  local top = b.step * 8 - 8 + dy
  local values = { { b.dp, b.max_dp }, { b.pp, b.max_pp } }
  d.anchor(c.place.anchor)
  d.layer(c.layer)
  for k = 2, 1, -1 do
    if b.target & k ~= 0 then
      local cur, max = values[k][1], values[k][2]
      local m = max == 0 and 1 or max
      d.stat_bar(c.bars[k].x + dx, top + 8, ((cur << 6) + m - 1) // m, c.bars[k].gauge)
    end
  end
  for k = 2, 1, -1 do
    if b.target & k ~= 0 then
      local bar, cur, max = c.bars[k], values[k][1], values[k][2]
      local px = bar.digits + dx - 5 * decimal_digits(max)
      local cx, cy = d.xy(px, top, 0xFFFEFFF9)
      d.value(1, cx, cy, cur)
      d.value(2, px, top, 0)
      d.value(0, bar.digits + dx, top, max)
      d.text_raw(bar.word, bar.x + dx, top - 1, c.colour)
    end
  end
end

-- gem_slots: the count by the raw printer (unk8), then the gem icon (unk8 + 4).
M.draw.gem_slots = function(e, d)
  local c = look.gem_slots
  local dx, dy = c.place.dx, c.place.dy
  d.anchor(c.place.anchor)
  d.layer("unk8")
  d.text_raw(c.prefix .. string.char(48 + e.gems.count), c.text[1] + dx, c.text[2] + dy, c.colour)
  d.layer("unk8", 4)
  d.sprite(c.icon[1] + dx, c.icon[2] + dy, c.sprite)
end

-- stat_page: _renderStatPage's calls in its order, recorded, then made last to first (each links at its slot's
-- head). The packed places as the game packs them (u16 x, y above).
local page_ops, page_n = {}, 0
local function page_op(kind, layer, a, b, c, w, x)
  page_n = page_n + 1
  local o = page_ops[page_n]
  if not o then o = {} page_ops[page_n] = o end
  o[1], o[2], o[3], o[4], o[5], o[6], o[7] = kind, layer, a, b, c, w, x
end
local function u16(v) return v & 0xFFFF end
local function int_pen(value, x, y) -- vs_mainMenu_renderIntColor's returned place (d.xy: its packed arithmetic)
  local n, v = 1, value < 0 and -value or value
  while v >= 10 do v, n = v // 10, n + 1 end
  return x - 8 * n, y
end
local function trunc_div(a, b) local q = a // b if q < 0 and q * b ~= a then q = q + 1 end return q end
M.draw.stat_page = function(e, d)
  local c, p = look.stat_page, e.page
  local s, xo, page = p.stats, p.x_offset + c.place.dx, p.page
  local dy = c.place.dy
  local rows = c.rows[page]
  if not rows then return end
  page_n = 0
  local v0, cmp0, eq0, extent = rows[1], rows[2], rows[3], rows[4]
  local best
  if page == 1 then
    best = s[v0]
    for i = 0, 6 do if best < s[v0 + i] then best = s[v0 + i] end end
  end
  local comparing = p.enabled == 7
  for row = 0, extent - 1, 16 do
    local j = row >> 4
    local gx = u16(xo + c.gauge_x)
    local y = c.top + row + dy
    local value = s[v0 + j]
    if s[63] ~= 0 and comparing and page ~= 2 then
      local diff = value - s[eq0 + j]
      local x = u16(xo + c.diff_x)
      page_op("int", -12, diff, 0 < diff and c.up or diff < 0 and c.down or c.white, nil, x, y)
      local px, py = int_pen(diff, x, y)
      if value > s[eq0 + j] then page_op("text", -12, "#+", px + 1, py, nil, c.up) end
      if value == s[eq0 + j] then page_op("text", -12, "#*", px + 1, py, nil, c.white) end
    end
    local colour = c.white
    local vx = u16(xo + c.value_x)
    if p.subtype == 1 and comparing and page ~= 0 then
      if page == p.subtype then
        local other = s[cmp0 + j]
        if other ~= value then
          colour = other < value and c.up or c.down
        else
          colour = other ~= best and c.grey or c.white
        end
        page_op("int", -12, value, colour, nil, vx, y)
        colour = c.grey
        if value == best then colour, best = c.white, 0xFFFF end
      else
        if j ~= s[36] - 1 then colour = c.grey end
        page_op("int", -12, value, colour, nil, vx, y)
      end
    else
      local t = s[cmp0 + j]
      page_op("int", -12, value, t < value and c.up or value < t and c.down or c.white, nil, vx, y)
    end
    local name = c.names[j + page * 8]
    if name then page_op("text", -12, name, u16(xo + c.name_x), y, nil, colour) end
    page_op("packet3", -12, c.frame, gx | ((row + 0x42 + dy) << 16), gx | ((row + 0x45 + dy) << 16))
    page_op("packet3", -12, c.frame_light, gx | (y << 16), gx | ((row + 0x47 + dy) << 16))
    local w = trunc_div(value * 24, 100)
    if w > 24 then w = 24 end
    if w < -24 then w = -24 end
    if w >= 0 then
      for i = 0, 3 do
        local ly = (row + i + 66 + dy) << 16
        page_op("packet3", -12, c.positive[i + 1], gx | ly, u16(xo + w + 116) | ly)
        if w == 24 - i then w = w - 1 end
      end
    else
      for i = 0, 3 do
        local ly = (row - i + 0x45 + dy) << 16
        page_op("packet3", -12, c.negative[i + 1], gx | ly, u16(xo + w + 0x74) | ly)
        if w == i - 24 then w = i - 23 end
      end
    end
    for k = 2, 5 do
      local ly = (row + k + 64 + dy) << 16
      page_op("packet3", -12, c.shade, u16(xo - k + 97) | ly, u16(xo - k + 142) | ly)
    end
    for k = 0, 7 do
      local ly = (row + k + 64 + dy) << 16
      page_op("packet3", -12, c.shade_dark, u16(xo - k + 97) | ly, u16(xo - k + 145) | ly)
    end
    page_op("packet1", -12, c.modes[1])
    for k = 0, 10 do
      local ly = (row + k + 0x40 + dy) << 16
      page_op("packet4", -12, u16(xo) | ly, u16(xo - (k - 0x60)) | ly)
    end
    page_op("packet1", -12, c.modes[2])
  end
  local win = c.window
  page_op("window", -12, xo + win[1], win[2] + dy, win[3], win[4])
  page_op("button", -16, xo + c.button[1], c.button[2] + dy)
  -- the header: its parts' styles as the game sets them (the page shown 1, an enabled one 0, the others 3)
  local x = u16(xo) + c.header.x
  for _, part in ipairs(c.header.parts) do
    local style = 0
    if part[2] then
      style = part[2] == page and c.styles.shown or ((p.enabled >> part[2]) & 1) ~= 0 and c.styles.enabled
        or c.styles.off
    end
    page_op("font", -16, part[1], x, c.header.y + dy, style + c.font_style)
    x = d.font_width(part[1], x)
  end
  d.anchor(c.place.anchor)
  for i = page_n, 1, -1 do
    local o = page_ops[i]
    local k = o[1]
    d.layer(o[2])
    if k == "int" then d.menu_int(o[3], o[6], o[7], o[4])
    elseif k == "text" then d.text_raw(o[3], o[4], o[5], o[7])
    elseif k == "packet3" then d.packet(o[3], o[4], o[5])
    elseif k == "packet1" then d.packet(o[3])
    elseif k == "packet4" then d.packet(c.line[1], o[3], c.line[2], o[4])
    elseif k == "window" then d.button_background(o[3], o[4], o[5], o[6])
    elseif k == "button" then d.sprite(o[3], o[4], c.sprite)
    elseif k == "font" then d.font_text(o[3], o[4], o[5], o[6])
    end
  end
end

-- items_status_button: the window, the button, the word (in the reverse of the game's calls).
M.draw.items_status_button = function(e, d)
  local c, row = look.items_status_button, e.items.row
  local dx, dy = c.place.dx, c.place.dy
  d.anchor(c.place.anchor)
  d.layer("unk8")
  d.text_raw(c.word[1], c.word[2] - row + dx, c.word[3] + dy, (c.colour << e.items.state) & 0xFFFFFF)
  d.sprite(c.button[1] - row + dx, c.button[2] + dy, c.sprite)
  d.layer(-12)
  local w = c.window
  d.button_background(w[1] - row + dx, w[2] + dy, w[3], w[4])
end

-- The screens' own strips: each game call recorded in its order (op: its kind, layer, arguments), then made last
-- to first (each links at its slot's head).
local strip_ops, strip_n = {}, 0
local function strip_op(kind, layer, a, b, c, w, x)
  strip_n = strip_n + 1
  local o = strip_ops[strip_n]
  if not o then o = {} strip_ops[strip_n] = o end
  o[1], o[2], o[3], o[4], o[5], o[6], o[7] = kind, layer, a, b, c, w, x
end
local function strip_flush(d)
  local sprite, u = look.buttons.sprite, look.buttons.u
  for i = strip_n, 1, -1 do
    local o = strip_ops[i]
    local k = o[1]
    if o[2] then d.layer(o[2]) else d.layer("unk8") end
    if k == "text" then d.text_raw(o[3], o[4], o[5], o[6])
    elseif k == "int" then d.menu_int(o[3], o[4], o[5], o[6])
    elseif k == "window" then d.button_background(o[3], o[4], o[5], o[6])
    elseif k == "button" then sprite.u = u[o[3]] d.sprite(o[4], o[5], sprite)
    end
  end
  strip_n = 0
end
local function s16(v) v = v & 0xFFFF return v >= 0x8000 and v - 0x10000 or v end

-- ok_mark: the word in its pulse's grey.
M.draw.ok_mark = function(e, d)
  local c, o = look.ok_mark, e.ok
  d.anchor(c.place.anchor)
  d.layer(c.layer)
  d.text_raw(c.word, o.x + c.place.dx, o.y + c.place.dy, o.level * c.grey)
end

-- discard_count: the units, tens and hundreds as the game splits them (vs_battle_toBCD twice), made last to first.
local function to_bcd(v) return v % 10 | (v // 10) * 16 end
local digit_chars = {} -- (the menu charset's digits: codes 0..9, vs_encode's "0".."9")
for i = 0, 9 do digit_chars[i] = tostring(i) end
M.draw.discard_count = function(e, d)
  local c, q = look.discard_count, e.discard
  local x, y = q.x + c.place.dx, q.y + c.place.dy
  local b = to_bcd(q.count)
  local units = b & 0xF
  b = to_bcd(b >> 4)
  d.anchor(c.place.anchor)
  d.layer("unk8")
  d.font_text(digit_chars[b >> 4], x - 2 * c.step, y, c.style)
  d.font_text(digit_chars[b & 0xF], x - c.step, y, c.style)
  d.font_text(digit_chars[units], x, y, c.style)
end

-- storage_buttons: the windows, the buttons, the words (the game's packed places: STATUS / END at (x + 12) as 16
-- bits, CANCEL 0x48 further, its carry into y kept).
M.draw.storage_buttons = function(e, d)
  local c, b = look.storage_buttons, e.storage
  local dx, dy, x = c.place.dx, c.place.dy, e.storage.x
  d.anchor(c.place.anchor)
  for _, w in ipairs(c.windows) do strip_op("window", c.window_layer, x + w[1] + dx, w[2] + dy, w[3], w[4]) end
  for _, t in ipairs(c.buttons) do strip_op("button", nil, t[1], x + t[2] + dx, t[3] + dy) end
  for _, t in ipairs(c.words) do
    local p = ((x + 12) & 0xFFFF) + (t[2] - 12)
    local word = t[5] == "lit" and t[4] << (b.state & 3) or t[5] == "dim" and t[4] >> (b.state >> 2) or t[4]
    strip_op("text", nil, t[1], s16(p) + dx, (t[3] | (p >> 16)) + dy, word)
  end
  strip_flush(d)
end

-- storage_counts: func_80106464's calls (capacity, "/", count) at a packed place, and the names.
local function counts(d, c, capacity, count, x, y)
  strip_op("int", nil, capacity, x, y, c.colour)
  local sx, sy = d.xy(x, y, c.slash_dx)
  strip_op("text", nil, c.slash, sx, sy, c.colour)
  strip_op("int", nil, count, sx, sy, count * 2 < capacity and c.low or c.colour)
end
M.draw.storage_counts = function(e, d)
  local c, t = look.storage_counts, e.storage
  local dx, dy = c.place.dx, c.place.dy
  d.anchor(c.place.anchor)
  if t.layout == 0 then
    local s, x = c.strip, t.x
    local nx, ny = d.xy(dx, s.name_y + dy, x)
    strip_op("text", nil, s.names[t.container], nx, ny, c.colour)
    for i = 1, 7 do
      local y = s.top + (i - 1) * s.line + dy
      local tx, ty = d.xy(dx, y, s.name_x + x)
      strip_op("text", nil, s.categories[i], tx, ty, c.colour)
      local cx, cy = d.xy(s.count_x + dx, y, x)
      counts(d, c, t.capacity[1][i], t.count[1][i], cx, cy)
    end
  else
    local s = c.summary
    for i = 1, 2 do strip_op("text", nil, s.heads[i], s.head_x[i] + dx, s.head_y + dy, c.colour) end
    for i = 1, 7 do
      local y = s.top + (i - 1) * s.line + dy
      counts(d, c, t.capacity[1][i], t.count[1][i], s.columns[1] + dx, y)
      strip_op("text", nil, s.categories[i], s.name_x + dx, y, c.colour)
      counts(d, c, t.capacity[2][i], t.count[2][i], s.columns[2] + dx, y)
    end
  end
  strip_flush(d)
end

-- loot_capacities: _displayInventoryCapacities' calls.
M.draw.loot_capacities = function(e, d)
  local c, l = look.loot_capacities, e.loot
  local x, dy = l.x + c.place.dx, c.place.dy
  d.anchor(c.place.anchor)
  strip_op("text", nil, c.head[1], x + c.head[2], c.head[3] + dy, c.colour)
  for i = 1, 7 do
    local colour, capacity, count = c.colour, l.capacity[i], l.count[i]
    if l.looking ~= 0 then
      colour = c.dim << ((l.looking == 1 or l.incoming[i] ~= 0) and 1 or 0)
    end
    local y = c.top + (i - 1) * c.line + dy
    strip_op("text", nil, c.slash, x + c.slash_x, y, colour)
    strip_op("text", nil, c.categories[i], x + c.name_x, y, colour)
    strip_op("int", nil, capacity, x + c.capacity_x, y, colour)
    if colour >> 23 ~= 0 then
      if count * 2 < capacity then colour = c.low end
      if l.looking == 2 and capacity < l.incoming[i] + count then colour = c.over end
    end
    strip_op("int", nil, count, x + c.count_x, y, colour)
  end
  for _, w in ipairs(c.windows) do strip_op("window", c.window_layer, x + w[1], w[2] + dy, w[3], w[4]) end
  for _, w in ipairs(c.words) do
    local lit = w[7] == "all" and (l.all and (l.controls & 1) or 0) or l.controls
    strip_op("text", nil, w[1], x + w[2], w[3] + dy, c.dim << lit)
    strip_op("button", nil, w[4], x + w[5], w[6] + dy)
  end
  strip_flush(d)
end

-- rename_grid: _renderRenameMenu's calls in its order (op: kind, slot, arguments), made last to first.
local grid_ops, grid_n = {}, 0
local function grid_op(kind, layer, unk8, a, b, c, d)
  grid_n = grid_n + 1
  local o = grid_ops[grid_n]
  if not o then o = {} grid_ops[grid_n] = o end
  o.kind, o.layer, o.unk8, o.a, o.b, o.c, o.d = kind, layer, unk8, a, b, c, d
  return o
end
local function grid_flush(d)
  local buttons = look.buttons
  for i = grid_n, 1, -1 do
    local o = grid_ops[i]
    if o.unk8 then d.layer("unk8", o.layer) else d.layer(o.layer) end
    local k = o.kind
    if k == "packet" then
      local w = o.a
      if o.b == 6 then d.packet(w[1], w[2], w[3], w[4], w[5], w[6])
      else d.packet(w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9]) end
    elseif k == "font" then d.font_text(o.a, o.b, o.c, o.d, o.step)
    elseif k == "text" then d.text_raw(o.a, o.b, o.c, o.d)
    elseif k == "button" then buttons.sprite.u = buttons.u[o.a] d.sprite(o.b, o.c, buttons.sprite)
    elseif k == "sprite" then
      local s = o.a
      if o.d then s.u, s.clut_x = o.d, o.step end
      d.sprite(o.b, o.c, s)
    end
  end
  grid_n = 0
end
local grid_words = {} -- (the packets' words, reused: one table a packet)
local grid_codes = {} -- (one-code strings of the name, reused)
local function words(n) local w = grid_words[n] if not w then w = {} grid_words[n] = w end return w end
M.draw.rename_grid = function(e, d)
  local c, r = look.rename_grid, e.rename
  local xo, dx, dy = r.x_offset, c.place.dx, c.place.dy
  local nw = 0
  d.anchor(c.place.anchor)
  for _, b in ipairs(c.buttons) do grid_op("button", 0, true, b[1], -xo + b[2] + dx, b[3] + dy) end
  grid_op("sprite", 0, true, c.pad.sprite, -xo + c.pad.x + dx, c.pad.y + dy)
  local bar = c.bar
  local near = lerp(bar.near[1], bar.near[2], bar.brightness, 0)
  local far = lerp(bar.far[1], bar.far[2], bar.brightness, 0)
  for i = 1, 4 do
    local fade = r.fade[i]
    local left = lerp(near, far, 128, 8 - fade) | bar.code
    local right = lerp(near, far, 128, fade)
    for j = 0, bar.rows - 1 do
      local y = (j + bar.top + i * bar.line) << 16
      nw = nw + 1
      local w = words(nw)
      w[1], w[2], w[3], w[4] = bar.modes[1], left, ((-xo + dx) & 0xFFFF) | y, right
      w[5], w[6] = ((-xo - j + bar.length + dx) & 0xFFFF) | y, bar.modes[2]
      grid_op("packet", -4, false, w, 6)
    end
  end
  local wd = c.words
  for i = 0, 3 do
    grid_op("font", -8, false, r.commands[i + 1], wd.x - xo + dx, wd.y - i * wd.line + dy, c.style).step = wd.step
  end
  local nm = c.name
  for j = 1, 20 do
    local code = r.name[j]
    if code ~= nm.blank then
      local s = grid_codes[j]
      if not s then s = {} grid_codes[j] = s end
      s[1] = code
      grid_op("font", 4, true, s, xo + (j - 1) * nm.step + nm.x + dx, nm.y + dy, c.style).step = nil
    end
  end
  if xo == 0 then
    local a = c.arrows
    local colour = r.lit and a.lit or a.dim
    local clut = r.lit and a.clut_lit or a.clut_dim
    for _, t in ipairs(a.left) do grid_op("text", 0, true, t[1], t[2] + dx, a.y + dy, colour) end
    local o = grid_op("sprite", 0, true, a.sprite, a.left.x - r.step + dx, a.sprite_y + dy, a.left.u)
    o.step = clut
    for _, t in ipairs(a.right) do grid_op("text", 0, true, t[1], t[2] + dx, a.y + dy, colour) end
    o = grid_op("sprite", 0, true, a.sprite, a.right.x + r.step + dx, a.sprite_y + dy, a.right.u)
    o.step = clut
    grid_op("packet", 0, true, c.window, 9)
  end
  local t = c.table
  for i = 0, 8 do
    grid_op("font", 4, true, r.table[i + 1], xo + t.x + dx, i * t.line + t.y + dy, c.style).step = t.step
  end
  local bg = c.background
  for j = 1, 16, 4 do
    nw = nw + 1
    local w = words(nw)
    w[1], w[2] = bg.mode, bg.top
    for k = 0, 3 do
      local p = bg.corners[j + k]
      w[3 + 2 * k] = (xo + dx + (p[1] | ((p[2] + dy) << 16))) & 0xFFFFFFFF
      if k < 3 then w[4 + 2 * k] = bg.colour end
    end
    grid_op("packet", -4, false, w, 9)
  end
  grid_flush(d)
end

-- rename_box: the box's polygon (a draw mode, then four corners: the top-left's colour with the code, the bottom-left's).
M.draw.rename_box = function(e, d)
  local c, b = look.rename_box, e.highlight
  local x, y = b.x + c.place.dx, b.y + c.place.dy
  d.anchor(c.place.anchor)
  d.layer("unk8")
  d.packet(c.mode, c.code, x | (y << 16), 0, (x + b.w) | (y << 16), 0, x | ((y + b.h) << 16), c.colour,
    (x + b.w) | ((y + b.h) << 16))
end

-- MENU7's file menu (file_menu, file_element): drawn at once in the game's order (DrawPrim's: no ordering table, so not
-- last to first), its words packed as the game packs them (32 bits, its ORs and carries kept).
local W = 0xFFFFFFFF
local file_widths -- (the printer's advances by code + 1, from look.file_menu.font.widths' runs)
local function file_clut(x, y) return (y << 6) | ((x >> 4) & 0x3F) end
local function file_uvc(u, v, cx, cy) return (u | (v << 8)) | (file_clut(cx, cy) << 16) end
-- _drawSprt: its page word (0x9FF of it), the colour 0x80 less the page's high half (signed), a 16-bit sprite.
local function file_sprite(d, xy, uvclut, wh, page)
  local rgb = (0x80 - (page // 65536)) * 0x010101
  d.packet(0xE1000000 | (page & 0x9FF), (0x64000000 | rgb) & W, xy & W, uvclut & W, wh & W)
end
-- _printCharacter: a code at x, y in colour `clut`; the pen after it.
local function file_char(d, f, c, x, y, clut)
  local adv = file_widths[c + 1] or f.cell
  if c == f.space then
    d.text_glyph(-1, x, y, adv)
  else
    d.text_glyph(c, x, y, adv)
    d.packet(f.mode, f.colour, ((x & 0xFFFF) | (y << 16)) & W,
      file_uvc((c % f.columns) * f.cell, (c // f.columns) * f.cell, clut * 16 + f.clut_x, f.clut_y),
      f.cell | (f.cell << 16))
  end
  return x + adv
end
-- _printString: the game's codes (to 0xE7; 0xE6 the blinking arrow, stepped from `arrow`; 0xE8 a new line; 0xFA a
-- move); the arrow's step after.
local function file_string(d, f, codes, x, y, clut, arrow)
  local i, start, pen = 1, x, x
  while true do
    local c = codes[i]
    if not c then break end
    i = i + 1
    if c < 0xEC then
      if c < 0xE5 then pen = file_char(d, f, c, pen, y, clut)
      elseif c == 0xE6 then
        arrow = (arrow + 1) % 12
        pen = file_char(d, f, f.arrow - (arrow >> 2), pen, y, clut)
      elseif c == 0xE7 then break
      elseif c == 0xE8 then y, pen = y + f.line, start end
    else
      local control = codes[i] or 0
      i = i + 1
      if c == 0xFA then pen = control >= 0xF0 and pen - (256 - control) or pen + control end
    end
  end
  return arrow
end
local function file_info(d, c, xy, id) -- _drawSaveInfoUI
  local t = c.info[id + 1]
  file_sprite(d, xy, file_uvc(t[1], t[2], t[3], c.info.clut_y), t[4] | (t[5] << 16), c.info.page)
end
local function file_integer(d, c, xy, value, place) -- _renderInteger
  local g = c.digits
  repeat
    file_sprite(d, xy, file_uvc((value // place) * g.w, 0, g.clut_x, g.clut_y), g.w | (g.h << 16), g.page)
    value, place, xy = value % place, place // 10, xy + g.step
  until place == 0
end
local function file_count(v) local n = 0 repeat v, n = v // 10, n + 1 until v == 0 return n end
local function file_gauge(d, c, xy, mp, value, max) -- _drawHPMP
  local g, h = c.gauge, c.hp_digits
  local fill = max ~= 0 and ((value * 64 + max - 1) & W) // max or 0
  local rgb0 = mp and g.mp or g.hp
  local f = 64 - fill
  local rgb1 = ((((fill * g.full[1]) + (rgb0 & 0xFF) * f) & W) >> 6)
    | (((((fill * g.full[2]) + ((rgb0 >> 8) & 0xFF) * f) & W) >> 6) << 8)
    | (((((fill * g.full[3]) + (rgb0 >> 16) * f) & W) >> 6) << 16)
  local back = xy + (8 << 16)
  xy = xy + 1
  d.packet(g.mode, g.back, back & W, g.w | (g.h << 16), (g.code | rgb1) & W, (xy + (9 << 16)) & W, rgb0,
    (xy + fill + (9 << 16)) & W, rgb1, (xy + (12 << 16)) & W, rgb0, (xy + (fill + (12 << 16))) & W)
  local cd, md = file_count(value), file_count(max)
  file_info(d, c, xy - 1, mp and c.info.ids.mp or c.info.ids.hp)
  xy = (xy - (cd * 6 - 55)) - md * 5
  local place = ({ 1, 10, 100 })[cd] or 0
  repeat
    file_sprite(d, xy - (1 << 16), (((value // place) << 3) + h.u) | (file_clut(c.digits.clut_x, c.digits.clut_y) << 16),
      h.w | (h.h << 16), c.digits.page)
    value, place, xy = value % place, place // 10, xy + h.step
  until place == 0
  file_info(d, c, xy + 1, c.info.ids.dot)
  file_integer(d, c, xy + 6, max, ({ 1, 10, 100 })[md] or 1)
end
local function file_mix(a, b, f) -- _interpolateRGB: a to b by f / 8, each byte
  local out = 0
  for shift = 16, 0, -8 do
    out = (out << 8) + ((((a >> shift) & 0xFF) * (8 - f) + ((b >> shift) & 0xFF) * f) >> 3)
  end
  return out
end
local function file_panel_colour(p, outer, inner) -- _intepolateMenuItemBgColour
  local c1, c2
  if inner < 9 then
    c1, c2 = file_mix(p.near[1], p.near[2], inner), file_mix(p.far[1], p.far[2], inner)
  else
    c1, c2 = p.lit[(inner >> 3) - 1], p.dark[(inner >> 3) - 1]
  end
  return file_mix(c1, c2, outer)
end

M.draw.file_element = function(e, d)
  local c, f = look.file_menu, e.file
  local font = c.font
  if not file_widths then
    file_widths = {}
    local w = font.widths
    for i = 1, #w, 2 do for _ = 1, w[i + 1] do file_widths[#file_widths + 1] = w[i] end end
  end
  if not (f.slot >= 5 or ((f.y - c.panel.top) & W) < c.panel.rows) then return end
  local p, outer, arrow = c.panel, f.outer, f.arrow
  local v1, v2 = file_panel_colour(p, 8 - outer, f.inner), file_panel_colour(p, outer, f.inner)
  local y = f.y << 16
  d.packet(p.mode, p.shadow, (((f.x + 2) & 0xFFFF) | ((f.y + 2) << 16)) & W, (f.w | (f.h << 16)) & W,
    p.code | v1, ((f.x & 0xFFFF) | y) & W, v2, (((f.x + f.w) & 0xFFFF) | y) & W, v1,
    ((f.x & 0xFFFF) | ((f.y + f.h) << 16)) & W, v2, (((f.x + f.w) & 0xFFFF) | ((f.y + f.h) << 16)) & W)
  local x, t, i = f.x + p.text_x, f.text, 1
  while t[i] do
    local code = t[i]
    if code == 0xFA then i = i + 1 x = x + (t[i] or 0)
    elseif code ~= 0xFF then x = file_char(d, font, code, x, f.y, 0)
    else break end
    i = i + 1
  end
  if f.slot >= 5 then return end
  local pic = c.picture
  local location, state = f.location - 1, f.slot_state
  if state < 3 then
    location = state == 0 and -2 or state == 2 and -4 or f.saving and -1 or -5
  elseif location >= pic.slots then
    location = -3
  end
  d.image_store(pic.clut_x, pic.clut_y, pic.clut_w, 1)
  d.image_load(pic.clut_x, pic.clut_y, pic.clut_w, 1, f.cluts[(location < 0 or location >= 32) and 2 or 1])
  local fade = (8 - outer) << 19
  if location < 0 then
    file_sprite(d, (f.x - 64) | y, (~location << 13) | file_uvc(pic.special, 0, pic.clut_x, pic.clut_y),
      pic.w | (pic.h << 16), fade | 0x9C)
  else
    file_sprite(d, (f.x - 64) | y, ((location & 8) * 8) | ((location & 7) << 13) | file_uvc(0, 0, pic.clut_x, pic.clut_y),
      pic.w | (pic.h << 16), (((((location & 0x30) * 4) + pic.page) & 0x3FF) >> 6) | fade | 0x90)
  end
  d.image_restore()
  local ids = c.info.ids
  file_info(d, c, (f.x - 22) | y, ids.number)
  file_integer(d, c, (f.x - 9) | y, f.slot + 1, 10)
  if state == 0 then
    arrow = file_string(d, font, f.in_use, f.x + 6, f.y + 10, 3, arrow)
  elseif state == 1 then
    if not f.saving then arrow = file_string(d, font, f.empty, f.x + 6, f.y + 10, 3, arrow)
    else arrow = file_string(d, font, f.fresh, f.x + 6, f.y + 10, 0, arrow) end
  else
    y = y + (4 << 16)
    arrow = file_string(d, font, f.place, f.x + 6, f.y + 4, 0, arrow)
    file_info(d, c, y | 172, ids.map)
    file_info(d, c, y | 189, ids.colon)
    if f.map == 100 then
      file_integer(d, c, y | 192, 100, 100)
      file_info(d, c, (y + 0xFFFF0000) | 207, ids.percent)
    else
      file_integer(d, c, y | 192, f.map, 10)
      file_info(d, c, (y + 0xFFFF0000) | 202, ids.percent)
    end
    file_info(d, c, y | 217, ids.save)
    file_info(d, c, y | 239, ids.colon)
    file_integer(d, c, y | 242, f.saves, 1000)
    file_info(d, c, y | 267, ids.clear)
    file_info(d, c, y | 293, ids.colon)
    file_integer(d, c, y | 296, f.clears, 10)
    y = y + (13 << 16)
    if f.clears ~= 0 then
      local b = c.clear_badge
      file_sprite(d, y | 69, file_uvc(b[1], b[2], b[3], b[4]), b[5] | (b[6] << 16), b.page)
    end
    file_info(d, c, y | 240, ids.time)
    if f.hours == 100 then file_integer(d, c, y | 263, f.hours, 100) else file_integer(d, c, y | 268, f.hours, 10) end
    file_info(d, c, y | 279, ids.colon)
    file_integer(d, c, y | 282, f.minutes, 10)
    file_info(d, c, y | 293, ids.colon)
    file_integer(d, c, y | 296, f.seconds, 10)
    file_gauge(d, c, y | 88, false, f.hp, f.hp_max)
    file_gauge(d, c, y | 158, true, f.mp, f.mp_max)
    y = y - (17 << 16)
  end
  local g = c.progress
  if f.progress == 2 then -- _fileProcessingCompleteAnim
    local colour = f.progress_colour
    local c0 = f.saving and colour + (colour << 15) or (colour - (colour >> 3)) + (colour << 8)
    local c1 = 0
    d.packet(g.mode, (g.tile | c0) & W, (y | 64) & W, 256 | (32 << 16))
    for _ = 1, 2 do
      local yo = y + (-8 << 16)
      d.packet((g.code | c1) & W, (yo | 64) & W, c1, (yo | 320) & W, c0, (y | 64) & W, c0, (y | 320) & W)
      y, c1, c0 = y + (40 << 16), c0, 0
    end
  elseif f.progress == 1 then -- _fileProcessingAnim
    local px = f.progress_x
    local left = px - g.edge
    if (left & W) < g.left then left = g.left end
    local g1, g2 = 0, f.saving and g.save or g.load
    for _ = 1, 2 do
      d.packet(g.mode, (g.code | g1) & W, (left | y) & W, g2, (px | y) & W, g1, (left | (y + (g.h << 16))) & W, g2,
        (px | (y + (g.h << 16))) & W)
      left, px, g1, g2 = px, px + g.step, g2, 0
    end
  end
end

M.draw.file_menu = function(e, d)
  local c, m = look.file_menu, e.file_menu
  local fr = c.frame
  if m.part == 0 then -- the band's two sprites (under the elements)
    local b = fr.band
    for _, s in ipairs(b) do
      file_sprite(d, s[1] | (s[2] << 16), file_uvc(0, b.v, b.clut_x, b.clut_y), s[3] | (s[4] << 16), s[5])
    end
    return
  end
  local r = fr.rows
  for i = 1, 3 do
    local y = r.top - i
    file_sprite(d, y << 16, y << 8, r.w[1] | (1 << 16), (m.x >> 6) | (((4 - i) << 21) | 256))
    file_sprite(d, 256 | (y << 16), y << 8, r.w[2] | (1 << 16), ((m.x + 256) >> 6) | (((4 - i) << 21) | 256))
  end
  local k = fr.caution
  file_sprite(d, k[1] | (k[2] << 16), file_uvc(k[3], k[4], k[5], c.info.clut_y), k[6] | (k[7] << 16), c.digits.page)
  local msg = fr.message
  d.packet(msg.tile, msg.y << 16, 320 | (msg.h << 16))
  if m.cursor_xy ~= 0 then
    local h = fr.hand
    file_sprite(d, m.cursor_xy, file_uvc(h.u, h.v, h.clut_x, c.info.clut_y), h.w | (h.h << 16),
      (h.saturation[m.cursor_color + 1] << 16) | c.digits.page)
  end
  if m.message then file_string(d, c.font, m.message, msg.x, msg.text_y, 0, m.arrow) end
  if m.fade ~= 0 then
    local z = fr.fade
    d.packet(z.mode, (z.code | (m.fade * z.step)) & W, 0, z.w | (z.h << 16))
  end
end

-- MENUF's screens (menuf): each recorded call to the game's leaves made as the leaf makes it (the polygons' words
-- packed as the game packs them), the calls last to first (the game's prepends to its slots: the reverse of its
-- calls). menuf_load: the screen's uploads, the game's own pixels (the record's handles), in their order.
local function clut_word(x, y) return (y << 6) | ((x >> 4) & 0x3F) end
local function xy16(x, y) return (x & 0xFFFF) | ((y & 0xFFFF) << 16) end
local function uv(u, v) return (u & 0xFF) | ((v & 0xFF) << 8) end
local function rgb(r, g, b) return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) end
local function grey(v) return rgb(v, v, v) end
local function channels(c) return c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF end
local function scaled(c, k, n) -- each channel * k // n (non-negative, C's division)
  local r, g, b = channels(c)
  return rgb(r * k // n, g * k // n, b * k // n)
end
local mf_ops, mf_n = {}, 0
local function mf_op(slot)
  mf_n = mf_n + 1
  local o = mf_ops[mf_n]
  if not o then o = { w = {} } mf_ops[mf_n] = o end
  o.slot, o.text, o.count = slot, nil, 0
  return o
end
-- A textured four-point polygon: flat (POLY_FT4, 9 words) or shaded (POLY_GT4, 12: the colours by corner).
local function mf_quad(code, colours, x, y, w, h, u, v, uw, clut, tpage)
  local o = mf_op(look.menuf.layer)
  local t = o.w
  x, y = x + look.menuf.place.dx, y + look.menuf.place.dy
  local x1, y1 = x + w, y + h
  if code & 0x10 == 0 then
    t[1], t[2], t[3], t[4], t[5] = (code << 24) | colours[1], xy16(x, y), uv(u, v) | (clut << 16), xy16(x1, y), uv(u + uw, v) | (tpage << 16)
    t[6], t[7], t[8], t[9] = xy16(x, y1), uv(u, v + h), xy16(x1, y1), uv(u + uw, v + h)
    o.count = 9
  else
    t[1], t[2], t[3] = (code << 24) | colours[1], xy16(x, y), uv(u, v) | (clut << 16)
    t[4], t[5], t[6] = colours[2], xy16(x1, y), uv(u + uw, v) | (tpage << 16)
    t[7], t[8], t[9] = colours[3], xy16(x, y1), uv(u, v + h)
    t[10], t[11], t[12] = colours[4], xy16(x1, y1), uv(u + uw, v + h)
    o.count = 12
  end
end
local mf_c = {}
local function mf_fade(c, brightness, clut)
  local f, m = look.menuf.fade, look.menuf
  if brightness <= 0 then return end
  if brightness < f.steps then
    mf_c[1] = grey(brightness * f.unit)
    mf_quad(0x2E, mf_c, c.x, c.y, c.w, c.h, c.u, c.v, c.w, clut + 1, c.tpage | m.semi)
  else
    mf_c[1] = grey(f.grey)
    mf_quad(0x2E, mf_c, c.x, c.y, c.w, c.h, c.u, c.v, c.w, clut, c.tpage)
  end
end
local function mf_shaded(c, left, right, clut, tpage)
  mf_c[1], mf_c[2], mf_c[3], mf_c[4] = left, right, left, right
  mf_quad(0x3E, mf_c, c.x, c.y, c.w, c.h, c.u, c.v, c.w, clut, tpage)
end
local function mf_tint(c)
  local m = look.menuf
  local code = c.colour1 >> 24
  if code == 0 then return end
  if code < m.fade.steps then
    mf_shaded(c, scaled(c.colour1, code, 8), scaled(c.colour2, code, 8), c.clut + 1, c.tpage | m.semi)
  else
    mf_shaded(c, c.colour1 & 0xFFFFFF, c.colour2 & 0xFFFFFF, c.clut, c.tpage)
  end
end
local function toward(c, t, to) -- _adjust: each channel (c * t + (4 - t) * to) / 4
  local r, g, b = channels(c)
  return rgb((r * t + (4 - t) * to) // 4, (g * t + (4 - t) * to) // 4, (b * t + (4 - t) * to) // 4)
end
local function mf_pop(c)
  local m, p = look.menuf, look.menuf.pop
  local code = c.colour1 >> 24
  if code == 0 then return end
  local left, right
  if code < m.fade.steps then
    left, right = scaled(c.colour1, code, 8), scaled(c.colour2, code, 8)
  elseif p.flash[code] then
    left = grey(p.flash[code]) right = left
  elseif code < p.settle then
    left, right = toward(c.colour1, code - p.dim, p.toward), toward(c.colour2, code - p.dim, p.toward)
  else
    left, right = c.colour1 & 0xFFFFFF, c.colour2 & 0xFFFFFF
  end
  if code < p.dim then mf_shaded(c, left, right, c.clut + 1, c.tpage | m.semi)
  else mf_shaded(c, left, right, c.clut, c.tpage) end
end
local function clamp(v, lo, hi) return v < lo and lo or (v > hi and hi or v) end
local function mf_wipe(c)
  local m, wp = look.menuf, look.menuf.wipe
  local edge = c.a
  if c.x + c.w < edge - wp.edge then
    mf_shaded(c, c.colour1 & 0xFFFFFF, c.colour2 & 0xFFFFFF, c.clut, c.tpage)
    return
  end
  if c.x >= edge then return end
  local x, u = c.x, c.u
  local i = 0
  while i < c.w do
    local seg = i + wp.strip >= c.w and c.w - i or wp.strip
    local a = clamp(edge - x, 0, wp.edge)
    local b = clamp(edge - (x + seg), 0, wp.edge)
    local left, right = scaled(c.colour1, a, wp.edge), scaled(c.colour2, b, wp.edge)
    mf_c[1], mf_c[2], mf_c[3], mf_c[4] = left, right, left, right
    mf_quad(0x3E, mf_c, x, c.y, seg, c.h, u, c.v, seg, c.clut + 1, c.tpage | m.semi)
    i, x, u = i + wp.strip, x + wp.strip, u + wp.strip
  end
end
local function mf_wheel(c)
  local m, wh = look.menuf, look.menuf.wheel
  local faded, plain = clut_word(wh.faded_clut[1], wh.faded_clut[2]), clut_word(wh.clut[1], wh.clut[2])
  local y, v = c.y, c.v
  for _ = 1, c.h do
    local level
    if y < wh.top then level = wh.grey - (wh.top - 1 - y) * wh.step
    elseif y >= wh.bottom then level = wh.grey - (y - wh.bottom) * wh.step end
    if level then
      if level < 0 then level = 0 end
      mf_c[1] = grey(level)
      mf_quad(0x2E, mf_c, c.x, y, c.w, 1, c.u, v, c.w, faded, c.tpage | m.semi)
      mf_quad(0x2E, mf_c, c.x, y, c.w, 1, c.u, v, c.w, faded, c.tpage | m.subtract)
    else
      mf_c[1] = grey(wh.grey)
      mf_quad(0x2E, mf_c, c.x, y, c.w, 1, c.u, v, c.w, plain, c.tpage)
    end
    y, v = y + 1, v + 1
  end
end
local function mf_area(c)
  local o = mf_op(look.menuf.layer)
  o.w[1] = 0xE3000000 | ((c.y & 0x3FF) << 10) | (c.x & 0x3FF)
  o.w[2] = 0xE4000000 | (((c.y + c.b - 1) & 0x3FF) << 10) | ((c.x + c.a - 1) & 0x3FF)
  o.count = 2
end
local mf_words = {}
local function mf_prompt(c, words)
  local p, b = look.menuf.prompt, look.buttons
  local step = c.a
  if step < 0 then return end
  if step > p.steps then step = p.steps end
  local x = c.x + p.dx - step * p.step
  for i = 1, 2 do
    local o = mf_op("unk8")
    o.text, o.x, o.y = words[i], x + p.text_dx + look.menuf.place.dx, c.y + (i - 1) * p.line + look.menuf.place.dy
  end
  local y = c.y + p.drop
  local blink = c.b & 0xF
  if blink < 9 then y = y - 16 + (blink >> 1) else y = y - 10 - (blink >> 2) end
  local s = b.sprite
  local u = b.u[p.button]
  mf_c[1] = 0
  mf_quad(0x2D, mf_c, x, y + p.top, p.w, p.h, u, s.v, p.w, clut_word(s.clut_x, s.clut_y),
    ((s.page_y & 0x100) >> 4) | ((s.page_x & 0x3FF) >> 6))
end
local function mf_string(codes)
  local s = ""
  for i = 1, #codes do s = s .. string.char(codes[i]) end
  return s
end
local mf_draws = { function(c) mf_fade(c, c.a, c.clut) end, function(c) mf_fade(c, c.a, c.b) end, mf_tint, mf_pop,
  mf_wipe, mf_wheel, mf_area } -- (by the call's op: classic_parity.h PC_CLASSIC_MENUF_*; 8 the prompt)
M.draw.menuf = function(e, d)
  local r = e.menuf
  local ops = mf_draws
  mf_words[1], mf_words[2] = mf_string(r.prompt[1]), mf_string(r.prompt[2])
  mf_n = 0
  for _, c in ipairs(r.calls) do
    if c.op == 8 then mf_prompt(c, mf_words) else ops[c.op](c) end
  end
  d.anchor("game")
  for i = mf_n, 1, -1 do
    local o = mf_ops[i]
    if o.slot == "unk8" then d.layer("unk8") else d.layer(o.slot) end
    if o.text then d.text_raw(o.text, o.x, o.y, look.menuf.prompt.colour)
    else
      local w = o.w
      if o.count == 2 then d.packet(w[1], w[2])
      elseif o.count == 9 then d.packet(w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9])
      else d.packet(w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12]) end
    end
  end
  mf_n = 0
end
M.draw.menuf_load = function(e, d)
  for _, u in ipairs(e.load.uploads) do
    if u.pixels then d.image_load(u.x, u.y, u.w, u.h, u.pixels) end
  end
end

for _, kind in ipairs({ "status", "chain", "gauges", "figure", "status_icons", "battle_mode", "ability_icons", "buff",
                        "damage", "actor_word", "timer", "emote", "text_frame", "text_band", "box_text", "image",
                        "target_row", "target_line", "target_marker", "menu_row", "menu_cursor", "menu_command",
                        "menu_tabs", "menu_background", "option_value", "items_category", "items_status_button", "equip_stats",
                        "dp_pp_bars", "gem_slots", "stat_page", "ok_mark", "storage_buttons",
                        "storage_counts", "loot_capacities", "discard_count", "rename_grid", "rename_box", "file_menu",
                        "file_element", "menuf", "menuf_load" }) do
  rood.hud.claim(kind, function(e, d) return M.draw[kind](e, d) end, { flavour = "classic" })
end

return M
