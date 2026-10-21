-- rood.classic's tests (rood_rpc.py lua-test --plugin rood.classic; lua_smoke runs them with the others,
-- at the title). The packet-for-packet check is the parity gate's (pc/tests/classic_parity.py); these
-- check the plugin's own contract: the primitives it draws with exist and refuse outside a draw, and in
-- the field with the classic look its kinds are drawn by it (the game's own drawing suppressed).
local t = rood.test

local function wait_for(cond, ticks)
  for _ = 1, ticks or 120 do
    if cond() then return true end
    t.wait(1)
  end
  return cond() and true or false
end

t.case("draws with the game's own pieces, only inside a draw", function()
  t.eq(type(rood.draw.text_raw), "function")
  t.eq(type(rood.draw.anchor), "function")
  t.raises(function() rood.draw.text_raw("$SLOW!", 44, 48, 0x208060) end, "only inside a HUD draw function")
  t.raises(function() rood.draw.anchor("left") end, "only inside a HUD draw function")
end)

t.case("the look reads the typography settings it uses", function()
  t.eq(type(rood.settings.get("ui_text_edge_margin")), "number")
  t.eq(type(rood.settings.get("ui_text_word_space")), "number")
end)

for _, c in ipairs({ { "status", { timing = "slow" }, "SLOW!" }, { "chain", { count = 3 }, "3 CHAINS" } }) do
  local kind, args, text = c[1], c[2], c[3]
  t.case(kind .. ": drawn by rood.classic in the classic look", function()
    if not rood.dev then return t.skip("no rood.dev (dev mode: ROOD_RPC or ROOD_DEV=1)") end
    if rood.hud.look() ~= "classic" then return t.skip("the modern look: rood.hud's") end
    rood.dev.clear()
    rood.dev.show(kind, args)
    local e
    local found = wait_for(function()
      for _, x in ipairs(rood.dev.list()) do
        if x.kind == kind and x.text == text and x.held then e = x return true end
      end
    end, 60)
    if not found or e.synthetic then
      rood.dev.clear()
      return t.skip(kind .. ": no game element here (the field is needed)")
    end
    t.ok(rood.hud.honoured(kind), kind .. ": honoured (hud_classic_" .. kind .. ")")
    t.ok(wait_for(function()
      for _, x in ipairs(rood.dev.list()) do
        if x.id == e.id and x.drawn >= 2 and x.suppressed then return true end
      end
    end, 120), kind .. ": drawn, the game's packets suppressed")
    rood.dev.clear(kind)
  end)
end

t.case("gauges: drawn by rood.classic in the classic look", function()
  if not rood.dev then return t.skip("no rood.dev (dev mode: ROOD_RPC or ROOD_DEV=1)") end
  if rood.hud.look() ~= "classic" then return t.skip("the modern look: the status display is hidden") end
  local function gauges()
    for _, x in ipairs(rood.dev.list()) do
      if x.kind == "gauges" then return x end
    end
  end
  if not wait_for(function() return gauges() ~= nil end, 30) then
    return t.skip("gauges: none here (the field is needed)")
  end
  local e = gauges()
  t.ok(e.max_hp > 0 and e.hp >= 0 and e.risk >= 0, "gauges: Ashley's HP / MP / RISK")
  t.ok(rood.hud.honoured("gauges"), "gauges: honoured (hud_classic_gauges)")
  t.ok(wait_for(function() local g = gauges() return g and g.drawn >= 2 and g.suppressed end, 120),
    "gauges: drawn, the game's packets suppressed")
end)
