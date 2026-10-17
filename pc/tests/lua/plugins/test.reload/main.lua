local VERSION = 1
rood.registry.register("test.reload", "test.reload/entry", { version = VERSION })
rood.on("tick", function() end)
rood.timer.every(1, function() end)
