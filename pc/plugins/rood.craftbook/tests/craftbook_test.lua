local t = rood.test
local craft = require("craft")

-- a craftable() list of our own: both orders of a pair, one with a different result
local list = {
  { kind = "blade", first = { record = 1, name = "Scimitar" }, second = { record = 2, name = "Hand Axe" },
    result = { name = "Goblin Club", material = "Bronze" } },
  { kind = "blade", first = { record = 2, name = "Hand Axe" }, second = { record = 1, name = "Scimitar" },
    result = { name = "Goblin Club", material = "Bronze" } },
  { kind = "armor", first = { record = 7, name = "Leather Glove" }, second = { record = 8, name = "Reinforced Glove" },
    result = { name = "Reinforced Glove", material = "Leather" } },
}

t.case("one line per result, both orders of a pair once", function()
  local lines = craft.lines(list)
  t.eq(#lines, 2)
  t.eq(lines[1], "Scimitar + Hand Axe: Goblin Club (Bronze)")
  t.eq(lines[2], "Leather Glove + Reinforced Glove: Reinforced Glove (Leather)")
end)

t.case("its setting", function()
  t.eq(type(rood.settings.get("plugin:rood.craftbook/show")), "boolean")
end)

t.case("the queries it reads", function()
  t.eq(type(rood.game.craftable()), "table")
  local w = rood.game.workshop()
  t.ok(w == nil or type(w.name) == "string")
end)
