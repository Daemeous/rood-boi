-- The craftbook's lines (main.lua draws them; tests/craftbook_test.lua calls this
-- with a list of its own).
local craft = {}

-- One line for each result a craftable() list can make: "First + Second: Result
-- (Material)". Both orders of a pair combine into the first's record; they are
-- listed apart only when their results differ.
function craft.lines(list)
  local lines, seen = {}, {}
  for _, c in ipairs(list) do
    local r = c.result
    local text = string.format("%s + %s: %s", c.first.name, c.second.name, r.name)
    if r.material then text = text .. " (" .. r.material .. ")" end
    local key = string.format("%s/%d/%d/%s/%s", c.kind, math.min(c.first.record, c.second.record),
      math.max(c.first.record, c.second.record), r.name, r.material or "")
    if not seen[key] then
      seen[key] = true
      lines[#lines + 1] = text
    end
  end
  return lines
end

return craft
