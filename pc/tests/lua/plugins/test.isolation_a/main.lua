secret = "only in a"
string.rep = nil -- changes this plugin's own string library only
rood.registry.register("test.shared", "test.isolation_a/greeter", {
  greet = function(name) return "hello " .. name .. " from " .. rood.plugin.id end,
  list = { 1, 2, 3 },
})
rood.settings.define("guarded", { type = "boolean", default = false }) -- test.isolation_b may read it, not change it
