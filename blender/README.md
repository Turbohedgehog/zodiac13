# blender

Everything for modelling in Blender via blender-mcp.

- `.uv/` — uv cache and Python 3.12 for the MCP server (ignored by git; the default cache fails on Windows with pywin32 error 32).
- `export/` — models exported from Blender (`.glb`/`.fbx`).
- Server config is `../.mcp.json` (it must stay in the project root). In Blender: `N` → BlenderMCP → Connect to Claude.
