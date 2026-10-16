---
name: drive-editor
description: Control the Ouroboros editor through its agent automation API — inspect and edit scenes, play the game, inject input, take screenshots, export builds. Use when asked to build or change a level, tweak entities, test gameplay, or verify visuals.
---

# Drive the Ouroboros editor

The editor exposes a JSON-lines API on `127.0.0.1:7420`. Requests run in order; the full reference is in AGENTS.md ("Agent automation API").

## Start an editor

- Interactive (the user watches): `OuroborosEditor --project SnakeGame/Snake.oproj`
- Headless (no window; automation/CI): `OuroborosEditor --project SnakeGame/Snake.oproj --headless [--port 0]`
- Batch: `--exec commands.jsonl --exit-after-exec`. Responses are printed to stdout, and the exit code is 1 if any command failed.

## Send commands

```bash
python Tools/obctl.py help
python Tools/obctl.py editor.state
python Tools/obctl.py entity.list '{"component": "MeshRenderer"}'
python Tools/obctl.py component.types
python Tools/obctl.py entity.create '{"name": "Crate", "components": {"MeshRenderer": {"Mesh": "Primitive:Cube"}, "Transform": {"Translation": [2, 0.5, 0]}}}'
python Tools/obctl.py component.set '{"entity": "Crate", "type": "MeshRenderer", "data": {"Color": [1, 0.3, 0.2, 1]}}'
python Tools/obctl.py editor.screenshot '{"path": "build/crate.png"}'
python Tools/obctl.py editor.screenshot '{"path": "build/editor.png", "ui": true}'
python Tools/obctl.py scene.save
```

The first screenshot captures the scene only; `"ui": true` captures the whole editor window (not available headless).

## Workflow

1. **Orient:** `editor.state`, `project.info`, `entity.list`, `asset.list`.
2. **Edit:** `entity.create`, `component.set` (a patch: only the given fields change), `component.add/remove`, `entity.destroy`. Refer to entities by ID or name.
3. **Verify visually:** `editor.camera.set {"position", "target"}`, then `editor.screenshot`, then open the PNG with the Read tool. `renderer.settings.set {"debugView": n}` helps diagnose lighting problems.
4. **Test gameplay:** `editor.play`, `editor.wait {"frames": 30}`, `input.tap {"key": "Up"}`, `script.exec {"code": "return ..."}`, `game.ui`, `script.errors`, `log.get {"level": "error"}`, then `editor.stop`. Play mode runs on a copy, and `editor.stop` discards runtime changes.
5. **Persist:** `scene.save` (edit mode only), `project.set` + `project.save`.
6. **Ship:** `project.export {"output": "Exports/Snake3D"}`.

## Tips

- In headless mode each frame advances exactly 1/60 s, so `editor.wait {"frames": 60}` is one second of game time.
- Mesh paths are asset-relative (`Meshes/Apple.gltf`) or primitives (`Primitive:Cube|Sphere|Plane`).
- `Metallic`/`Roughness` on MeshRenderer are overrides: −1 keeps the material's value.
- When a command fails, the reason is in the response's `error` field, and nothing was changed.
