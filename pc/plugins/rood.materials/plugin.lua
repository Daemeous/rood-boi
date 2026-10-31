return {
  id = "rood.materials",
  name = "Materials (Lua)",
  version = "1.0.0",
  api_version = 1,
  description = "Window > Materials written in Lua on RPC material_instance: the instances as a tree, the master's "
    .. "parameter rows (reset to parent, inherited values greyed with their source), the assignment, Save / Revert. "
    .. "Off by default: the C++ window is Window > Materials; on, this one replaces it.",
  author = "rood-boi",
  default_enabled = false,
  capabilities = { "dev.tools" },
}
