-- The photo mode foundation in one session (pc/docs/Extensions.md "Photo mode foundation").
-- Not shipped: pc/tests/photo_foundation_smoke.py loads it, grants its capabilities and drives it
-- from the REPL (M.start(), then M.state). Tomorrow's rood.photo builds its menu on these calls.
local M = { state = "idle", events = {} }

-- Everything given back: the order does not matter, and each also goes by itself when the plugin
-- unloads, reloads or is disabled.
function M.finish()
  rood.input.release()
  rood.ui.show()
  rood.post.reset()
  rood.camera.finish()
  rood.game.resume()
  M.state = "done"
end

function M.start()
  local ok, why = rood.game.pause() -- from the next vsync; the game's state is held from then on
  if not ok then
    M.state = "refused: " .. why
    return false
  end
  assert(rood.camera.begin())      -- the editor's free camera, starting at the game's
  M.before = rood.camera.get()
  assert(rood.camera.orbit(35, 12)) -- degrees about the target (Ashley), in view space
  assert(rood.post.set("dof", true))
  assert(rood.post.set("dof_fstop", 1.4))
  assert(rood.post.set("exposure", 0.7))
  assert(rood.post.set("vignette", 0.4))
  assert(rood.ui.hide { hud = true, game2d = true, overlay = true })
  assert(rood.input.capture(function(e) -- the game sees nothing while held
    M.events[#M.events + 1] = e
    if e.type == "button" and e.name == "cross" and e.down then
      rood.camera.orbit(-10, 0)
    end
  end))
  M.path = assert(rood.capture.photo {
    done = function(result) -- once the frame with all of the above is presented and written
      M.photo = result
      M.finish()
    end,
  })
  M.state = "capturing"
  return M.path
end

rood.on("test.photo/start", M.start)
rood.on("photo.released", function(e) M.released = e end) -- the safety (Escape, Start) took them back
return M
