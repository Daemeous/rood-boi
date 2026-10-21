return {
  id = "test.photo",
  name = "Photo mode foundation example",
  version = "1.0.0",
  api_version = 1,
  description = "Pauses, orbits the camera, sets depth of field and exposure, hides the HUD, takes a photo, "
    .. "gives everything back (pc/docs/Extensions.md \"Photo mode foundation\"; a test fixture, not shipped).",
  capabilities = { "game.pause", "camera", "post", "ui.hide", "capture", "input.capture" },
}
