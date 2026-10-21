-- rood.classic (shipped, on): the classic look (ui_menus 0) drawn from Lua (pc/docs/Extensions.md,
-- "The classic look in Lua"; plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md).
--
-- The kinds: the timing word (status), the chain count (chain) and the status display's HP / MP / RISK
-- gauges with their digits and labels (gauges; the rest of the display, Ashley's figure, the status icons,
-- BATTLE MODE, stays the game's for now).
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
-- (the gauges are up whenever the field is: M.look.gauges.place.dx = 6 moves them at the next frame)
-- The REPL (pc/tools/rood_repl.py --plugin rood.classic) has this file's module as M: M.look.typeset.margin = 4
-- shows on the held word at the next frame; saving the file brings the file's values back.
-- Turned off (the Lua console's Plugins list), broken, or a kind's hud_classic_<kind> setting off: the
-- game draws them itself (the fallback, typography from ui_text's defaults).
local M = { draw = {} }
local S = rood.settings.get

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
    overrides = { status = { "typography" }, chain = { "typography" }, gauges = { "typography" } },
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

for _, kind in ipairs({ "status", "chain", "gauges" }) do
  rood.hud.claim(kind, function(e, d) return M.draw[kind](e, d) end, { flavour = "classic" })
end

return M
