-- rood.hud's tests (rood_rpc.py lua-test --plugin rood.hud; pc/tests/hud_bench_smoke.py runs them
-- in the field with the modern look; lua_smoke at the title with the classic one). Each kind is
-- summoned with rood.dev.show, then the test waits (on the condition, a bounded number of game
-- ticks: the menus and the reloads run in real time) for its element: its fields and, where the
-- claim is honoured, frames drawn by this plugin. The cases run side by side (coroutines): the
-- ones that summon take turns.
local t = rood.test

-- Polls cond each game tick, at most `ticks` of them.
local function wait_for(cond, ticks)
  for _ = 1, ticks or 120 do
    if cond() then return true end
    t.wait(1)
  end
  return cond() and true or false
end

-- The held element of kind showing text (others may still be playing out).
local function element(kind, text)
  for _, e in ipairs(rood.dev.list()) do
    if e.kind == kind and e.text == text and e.held then return e end
  end
end

-- Shows kind, waits for its element showing text, checks its fields (check(e)) and that this
-- plugin draws it when its claim is honoured; clears it. away: no element expected in the field
-- (the classic look's Travel prompt is the game's action bar, not ours).
local busy = false
local function summon_now(kind, args, text, check, away)
  rood.dev.clear()
  rood.dev.show(kind, args)
  local e
  local found = wait_for(function() e = element(kind, text) return e ~= nil end, 60)
  if not found and away and away() then
    rood.dev.clear()
    return t.skip(kind .. ": no element with the classic look in the field")
  end
  t.ok(found, kind .. ": an element appears")
  check(e)
  -- (a synthetic element is drawn only in the modern look; the classic look's claims draw at the game's hooks)
  if rood.hud.honoured(kind) and not (e.synthetic and rood.hud.look() == "classic") then
    t.ok(wait_for(function() e = element(kind, text) return e and e.drawn >= 2 end, 120),
      kind .. ": drawn by rood.hud (" .. tostring(e and e.drawn) .. " frames)")
    t.ok(e.synthetic or e.suppressed, kind .. ": the game's own drawing skipped")
  end
  rood.dev.clear(kind)
end
local function summon(kind, args, text, check, away)
  if not rood.dev then return t.skip("no rood.dev (dev mode: ROOD_RPC or ROOD_DEV=1)") end
  while busy do t.wait(1) end
  busy = true
  local ok, err = pcall(summon_now, kind, args, text, check, away)
  busy = false
  if not ok then error(err, 0) end
end

t.case("claims the six kinds", function()
  t.eq(rood.hud.kinds(), { "status", "chain", "buff", "damage", "travel", "notice" })
end)

t.case("status: SLOW!", function()
  summon("status", { timing = "slow" }, "SLOW!", function(e)
    t.eq(e.text, "SLOW!")
    t.eq(e.timing, "slow")
    t.eq(e.anchor, "left")
    t.ok(e.w > 0 and e.h > 0, "its box")
  end)
end)

t.case("chain: 3 CHAINS", function()
  summon("chain", { count = 3 }, "3 CHAINS", function(e)
    t.eq(e.text, "3 CHAINS")
    t.eq(e.value, 3)
    t.eq(e.life, 91)
  end)
end)

t.case("buff: WEAPON HUMAN +2, its four boxes", function()
  summon("buff", { slot = "weapon", stat = "human", value = 2 }, "WEAPON HUMAN +2", function(e)
    t.eq(e.text, "WEAPON HUMAN +2")
    t.eq({ e.slot, e.stat, e.sign, e.value }, { "WEAPON", "HUMAN", 1, 2 })
    t.eq(#e.parts, 4)
    t.eq({ e.parts[1].text, e.parts[2].text, e.parts[3].text, e.parts[4].text }, { "WEAPON", "HUMAN", "+", "2" })
    for i = 1, 4 do t.ok(e.parts[i].w > 0 and e.parts[i].h > 0, "part " .. i .. "'s box") end
    t.ok(e.parts[2].x > e.parts[1].x and e.parts[4].x > e.parts[3].x, "left to right")
  end)
end)

t.case("damage: 123 over Ashley", function()
  summon("damage", { value = 123 }, "123", function(e)
    t.eq({ e.text, e.value, e.sign, e.pool }, { "123", 123, -1, "hp" })
    t.eq(e.parts[1].text, "123")
    t.eq(type(e.bounce), "number")
    t.eq(e.anchor, "centre")
  end)
  summon("damage", { value = 45, heal = true, pool = "mp" }, "45", function(e)
    t.eq({ e.text, e.sign, e.pool }, { "45", 1, "mp" })
  end)
end)

t.case("travel: the prompt", function()
  summon("travel", nil, "TRAVEL", function(e)
    t.eq({ e.text, e.glyph, e.anchor }, { "TRAVEL", "travel", "picture" })
    t.eq(e.corner, rood.settings.get("qol_fast_travel_prompt_anchor"))
    t.ok(e.unit > 0, "game pixels per layout unit")
  end, function() return not rood.hud.honoured("travel") end)
end)

t.case("notice: the save state slots' line", function()
  summon("notice", { text = "Saved to slot 3", value = 3 }, "Saved to slot 3", function(e)
    t.eq({ e.text, e.slot, e.anchor, e.corner }, { "Saved to slot 3", 3, "picture", 1 })
    t.ok(e.unit > 0, "game pixels per layout unit")
    t.ok(e.life > 0, "a life")
  end)
end)

t.case("the look follows the settings it reads", function()
  t.eq(type(rood.settings.get("qol_fast_travel_prompt_size")), "number")
  t.eq(#rood.settings.get("qol_fast_travel_prompt_color"), 4)
  t.eq(type(rood.settings.get("ui_text_status_size")), "number")
  t.raises(function() rood.settings.set("ui_prompt_gap", 3) end, "read%-only")
end)

t.case("typography: an override for this run, then the settings' again", function()
  local size = rood.settings.get("ui_text_status_size")
  rood.hud.typography("status", { size = size + 0.5 })
  t.eq(rood.settings.get("ui_text_status_size"), size, "the setting is not changed")
  t.raises(function() rood.hud.typography("status", { bogus = 1 }) end, "unknown field")
  rood.hud.typography("status", nil)
  t.eq(rood.settings.get("ui_text_status_size"), size)
end)
