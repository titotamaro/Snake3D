# AGENTS.md — Ouroboros engine & Snake 3D

Guidelines for humans and AI agents working in this repository. Read this before changing code.

**Ouroboros** is a small 3D game engine (C++20, CMake, Vulkan through NVRHI) with an editor that agents can fully control over a JSON API.
**Snake 3D** (`SnakeGame/`) is the game built with it.

## Repository layout

| Path | Contents |
|---|---|
| `Ouroboros/src/Ouroboros/` | Engine library (`Core`, `Math`, `Scene`, `Renderer`, `Asset`, `Physics`, `Audio`, `Scripting`, `ImGui`, `Project`, `Automation`) |
| `Ouroboros/Resources/Shaders/` | HLSL shaders (`<Name>.<vs\|ps\|cs>.hlsl`), compiled to SPIR-V by DXC at build time |
| `OuroborosEditor/` | The editor (Ouroboros Studio): panels, gizmos, play mode, agent command server |
| `OuroborosRuntime/` | The player shipped with exported games (no editing) |
| `OuroborosTests/` | doctest unit tests, GPU tests, integration test data (`Data/*.jsonl`) |
| `SnakeGame/` | The game project: `Snake.oproj`, `Assets/{Scenes,Scripts,Meshes,Textures,Audio,Environments}` |
| `Tools/GenerateAssets.py` | Regenerates the game's meshes, textures and sounds (deterministic) |
| `Tools/obctl.py` | Command-line client for the editor's agent API |
| `cmake/` | Dependencies (pinned FetchContent), compiler settings, shader compilation |
| `.claude/skills/` | Task-specific instructions for agents (build/test, driving the editor, extending the engine, code review) |

## Building

Requirements: CMake ≥ 3.24, a C++20 compiler, the **Vulkan SDK** (for `dxc`; `VULKAN_SDK` must be set) and a GPU/driver with **Vulkan 1.3**. All other dependencies (GLFW, NVRHI, glm, EnTT, spdlog, nlohmann/json, Jolt, Lua 5.4, sol2, Dear ImGui, ImGuizmo, miniaudio, cgltf, stb, doctest) are fetched at configure time at pinned versions.

```bash
# Windows (Visual Studio 2022)
cmake --preset windows
cmake --build --preset windows-debug      # or windows-release
ctest --preset windows-debug

# Linux / macOS (Ninja Multi-Config)
cmake --preset ninja
cmake --build --preset ninja-debug
ctest --preset ninja-debug
```

Windows notes for agents:
- Use the CMake bundled with Visual Studio (`...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`). A MinGW CMake on `PATH` may fail TLS verification when downloading dependencies.
- Run MSBuild arguments such as `/m` from PowerShell; Git Bash rewrites `/m` into a path.

Executables are written to `build/<preset>/bin/<Config>/`, and each one gets a copy of the engine `Resources/` folder next to it.

## Running

```bash
OuroborosEditor --project SnakeGame/Snake.oproj                 # editor (agent API on 127.0.0.1:7420)
OuroborosEditor --project SnakeGame/Snake.oproj --headless      # editor without a window (automation only)
OuroborosRuntime --project SnakeGame/Snake.oproj                # play the game
OuroborosRuntime --project SnakeGame/Snake.oproj --headless --frames 120 --screenshot out.png --test
```

Runtime options: `--scene <asset path>`, `--headless`, `--frames <n>`, `--screenshot <png>`, `--size WxH`, `--test`, `--validation`, `--debug-view <0-5>`, `--no-audio`.
Editor options: `--headless`, `--size WxH`, `--port <n>`, `--no-server`, `--exec <file.jsonl>`, `--exit-after-exec`, `--validation`, `--no-audio`.

Headless runs advance exactly 1/60 s per frame, so they are deterministic.

In-game controls: arrow keys or WASD to steer, P/Escape to pause, Space to restart after game over.
Editor controls: right mouse orbits, middle mouse pans, the wheel zooms, right mouse + WASD flies. W/E/R choose the gizmo mode, Q hides it, F focuses the selection, Del deletes it. Ctrl+D duplicates, Ctrl+S saves, Ctrl+P toggles play mode.

## Testing — required for every change

```bash
ctest --preset windows-debug            # everything
ctest --preset windows-debug -L unit    # CPU-only unit tests
ctest --preset windows-debug -LE gpu    # machines without a GPU
```

| Test | What it covers |
|---|---|
| `UnitTests` | doctest suites: Core, Math, Scene (ECS, registry, serialization), Scripting (whole Lua API, error handling, SnakeLogic rules), Physics, Audio, Assets (primitives, glTF, images), Project/export, Automation (commands, TCP server) |
| `GpuTests` | Headless Vulkan device, shader loading, mesh/texture/IBL caches, scene rendering, picking, readback, every debug view; fails on any validation error |
| `FeatureTestScene` | Runs `Scenes/FeatureTest.oscene`. `Scripts/Tests/FeatureTest.lua` exercises **every component and the complete scripting API** (128 checks) |
| `SnakeGameSmoke` | Plays the game's start scene for 240 frames with validation on |
| `EditorAutomation` | Drives the headless editor through the agent API (`OuroborosTests/Data/EditorAutomation.jsonl`) |
| `ExportGame` → `RunExportedGame` | Exports the project through the editor, then runs the exported executable |

Rules:
- New engine code needs unit tests. A new component, script function or agent command must also be covered by `FeatureTest.lua` (components and scripting) or `AutomationTests.cpp` / `EditorAutomation.jsonl` (commands).
- `--test` runs and the GPU tests fail on any logged error or Vulkan/NVRHI validation error. Keep them at zero.
- Warnings are errors (`/W4 /WX`, `-Wall -Wextra -Werror`). Build both Debug and Release before committing, because release builds compile out asserts and can expose unused variables.

## Code style (Hazel engine conventions)

- **Naming:** types, functions, methods, enums and namespaces use `PascalCase`. Locals and parameters use `camelCase`. Private members are `m_PascalCase`, statics are `s_PascalCase`. Public struct fields (components) are `PascalCase`. Macros are `OB_UPPER_CASE`. Source files are `PascalCase.cpp/.h`, one main class per file.
- **Formatting:** tabs for indentation, Allman braces, everything inside `namespace Ouroboros {` is indented, `#pragma once`. See `.clang-format`.
- **Includes:** the first line of every engine `.cpp` is `#include "obpch.h"`, then the file's own header, then other Ouroboros headers, then third-party headers, all sorted.
- **Ownership:** use `Ref<T>` (`std::shared_ptr`) and `Scope<T>` (`std::unique_ptr`) with `CreateRef`/`CreateScope`. No raw `new`/`delete` except for Layers (owned by the Application).
- **Errors:** `OB_CORE_ASSERT` (debug only) checks programming errors, `OB_CORE_VERIFY` checks fatal conditions. Recoverable failures return `bool`/`std::optional` with an error string and log with `OB_CORE_ERROR`. Never crash on bad user data (scenes, scripts, assets, commands).
- **Logging:** `OB_CORE_*` for the engine, `OB_*` for game and script output.
- **Comments:** explain *why*, not what. Public headers carry a short description of each class's contract.
- Prefer clear, explicit code over clever templates. Match the surrounding code.

## Architecture

- **Application** (`Core/Application`) owns the Window (GLFW), GraphicsContext (Vulkan 1.3 + NVRHI), Renderer, ImGuiLayer and audio. Each frame it calls `Layer::OnUpdate`, then `OnRender(commandList)`, then `OnImGuiRender`, then presents.
- **Scene** (`Scene/`) wraps an EnTT registry. Every entity has `IDComponent` (53-bit UUID), `TagComponent` and `TransformComponent`. Other components: Camera, MeshRenderer, DirectionalLight, PointLight, SkyLight, RigidBody, BoxCollider, SphereCollider, Script, AudioSource. `ComponentRegistry` gives type-erased add/remove/serialize/patch/copy by name, used by the serializer, scripts, agent commands and the editor.
- **Play mode:** `Scene::OnRuntimeStart` creates `PhysicsWorld` (Jolt), `SceneAudio` (miniaudio) and `ScriptRuntime` (Lua). The editor plays a *copy* of the edited scene (`Scene::Copy`). Updates run scripts each frame and physics at a fixed 60 Hz (`OnFixedUpdate`, then the physics step, then contact callbacks).
- **Systems sync with components:** physics and scripts scan the registry every step. Adding or removing components at runtime creates or destroys bodies and script instances, and changed collider settings rebuild bodies.
- **Renderer** (`Renderer/`): `Renderer` holds shared GPU state and the asset caches (meshes from glTF or `Primitive:Cube|Sphere|Plane`, textures, IBL environments). `SceneRenderer` runs these passes: 4-cascade PCSS soft shadows → depth/normal/entity-ID pre-pass → SSAO + bilateral blur → forward PBR (GGX, image-based lighting from HDRIs, point lights) → sky → editor grid → ACES tonemapping, sRGB conversion, dithering and the selection outline.
- **Conventions:** right-handed, Y up, −Z forward, meters, Euler rotations in radians (XYZ). Clip space is Y up with depth 0..1, because NVRHI applies D3D viewport conventions on Vulkan. Texture UV (0,0) is the top-left corner (see `UVToClip`/`ClipToUV` in `Common.hlsli`).
- **Automation** (`Automation/`): `CommandProcessor` (JSON commands, deferred results), `CommandServer` (TCP), `SceneCommands` (engine-level commands). The editor registers editor, scene, project, renderer and asset commands.

## File formats

- **Project** (`.oproj`, JSON): `Name`, `StartScene` (asset-relative), `AssetDirectory`, `Window {Title, Width, Height, Fullscreen, VSync}`.
- **Scene** (`.oscene`, JSON): `{"Ouroboros": "Scene", "Version": 1, "Name", "Entities": [{"UUID", "Components": {"<Type>": {fields...}}}]}`. Field names match the C++ component fields; vectors are arrays. `component.types` (agent API) lists every component with its default values.
- Asset paths are relative to the project's `Assets/` directory.

## Scripting API (Lua 5.4)

A script returns a class table. Each entity with a `Script` component gets an instance whose metatable is that class, with `self.Entity` set. Values in the class's `Properties` table are copied onto the instance and can be overridden per entity through `Script.Fields`. Callbacks: `OnCreate()`, `OnUpdate(dt)`, `OnFixedUpdate(dt)`, `OnDestroy()`, `OnCollisionBegin(other)`, `OnCollisionEnd(other)`, `OnTriggerBegin(other)`, `OnTriggerEnd(other)`. `require("Module")` loads `Assets/Module.lua` or `Assets/Scripts/Module.lua`. The `io`, `os` and `debug` libraries are not available.

- **Math:** `vec2/vec3/vec4(...)` with `+ - * /`, unary minus, `==`, `tostring`, `x y z w`, `:Length() :Normalized() :Dot() :Cross() :Distance() :Lerp()`; `quat(w,x,y,z)`, `quat.FromEuler(v) quat.AngleAxis(a, axis)`, `q:ToEuler() :Rotate(v) :Normalized() :Slerp(q2, t)`, `q * q2`, `q * v`; `Math.Pi Lerp Clamp Radians Degrees Sign MoveTowards`.
- **Core:** `Log.Trace/Info/Warn/Error(...)`, `print(...)`, `Ouroboros.Version`; `Time.GetDeltaTime GetElapsed GetFrameCount GetTimeScale SetTimeScale GetFixedDeltaTime`; `Random.Seed(n) Int(min, max) Float([min, max])`; `Application.Quit() GetViewportSize()`.
- **Input:** `Input.IsKeyDown/IsKeyPressed/IsKeyReleased(Key.X or "Name")`, `IsMouseButtonDown/IsMouseButtonPressed(Mouse.Left)`, `GetMousePosition() GetMouseDelta() GetScrollDelta()`. `Key` holds every key (`Key.W`, `Key.Space`, `Key.Up`, `Key.D1`, ...).
- **Scene:** `Scene.GetName() CreateEntity([name]) DestroyEntity(e) DuplicateEntity(e) FindEntityByName(name) GetEntityByID(id) GetEntities() GetEntitiesWithComponent(type) GetPrimaryCamera() GetEntityCount() Load(path)`.
- **Entity:** `e.ID`, `e.Name`, `e:IsValid() Destroy() HasComponent(type) AddComponent(type, [data]) RemoveComponent(type) GetComponentData(type) SetComponentData(type, data) GetScript() AddScript(path, [fields])`, `==`, `tostring`. Component proxies (nil if absent): `e.Transform` (Translation, Rotation, Scale, `GetForward/GetRight/GetUp/GetRotationQuat/SetRotationQuat/LookAt`), `e.Camera` (FieldOfView in degrees, Projection, PerspectiveNear/Far, OrthographicSize/Near/Far, Exposure, Primary), `e.MeshRenderer`, `e.DirectionalLight`, `e.PointLight`, `e.SkyLight`, `e.RigidBody` (fields plus LinearVelocity, AngularVelocity, `AddForce/AddImpulse/AddTorque/HasBody`), `e.BoxCollider`, `e.SphereCollider`, `e.AudioSource` (fields plus `Play/Stop/IsPlaying`).
  Proxies return copies of vectors: `e.Transform.Translation.x = 1` has no effect. Assign the whole vector instead.
- **Physics:** `Physics.GetGravity() SetGravity(v) Raycast(origin, direction, [maxDistance])` returns `{Entity, Position, Normal, Distance}` or nil.
- **Audio:** `Audio.PlayOneShot(path, [volume]) SetMasterVolume(v) GetMasterVolume()`.
- **UI:** `UI.Text(text, x, y, {Size, Color = vec4, Anchor = "TopLeft"|"Top"|"TopRight"|"Left"|"Center"|"Right"|"BottomLeft"|"Bottom"|"BottomRight"})`. Call it every frame; positions are in pixels relative to the anchor.

Errors in scripts are logged (`[Script] ...`), and the failing callback is disabled for that instance so one broken script cannot flood the log.

## Agent automation API

The editor listens on `127.0.0.1:7420` (or `--port`). Send one JSON request per line:
`{"id": 1, "cmd": "entity.list", "args": {}}` returns `{"id": 1, "ok": true, "result": [...]}` or `{"id": 1, "ok": false, "error": "..."}`.
Requests run in order. Deferred commands (`editor.wait`, `editor.step`, `editor.screenshot`, `input.tap`) answer once they complete. Use `python Tools/obctl.py <cmd> '<json args>'`, `--exec <file.jsonl>`, or `{"cmd": "help"}` for the live list.

| Area | Commands |
|---|---|
| General | `ping`, `help {command?}` |
| Editor | `editor.state`, `editor.play`, `editor.stop`, `editor.pause {paused?}`, `editor.step {frames?}`, `editor.wait {frames?, seconds?}`, `editor.select {entity}`, `editor.camera.get`, `editor.camera.set {position, target}`, `editor.camera.focus {entity}`, `editor.gizmo {mode}`, `editor.screenshot {path, ui?}`, `editor.viewport.resize {width, height}` (headless), `editor.quit {exitCode?}`, `game.ui` |
| Scene | `scene.info`, `scene.get`, `scene.set {scene}`, `scene.new {name?}`, `scene.open {path}`, `scene.save {path?}`, `scene.createPreset {preset}` |
| Entities | `entity.list {component?}`, `entity.get {entity}`, `entity.create {name?, components?}`, `entity.destroy`, `entity.duplicate`, `entity.rename {entity, newName}` |
| Components | `component.types`, `component.get {entity, type}`, `component.add {entity, type, data?}`, `component.set {entity, type, data}` (patch), `component.remove {entity, type}` |
| Scripts / input | `script.exec {code}` (play mode), `script.errors`, `input.key {key, down}`, `input.tap {key, frames?}` |
| Project / assets | `project.info`, `project.set {...}`, `project.save`, `project.export {output, executableName?}`, `asset.list {directory?, extension?}`, `asset.reload` |
| Renderer / log | `renderer.settings.get`, `renderer.settings.set {shadows?, ssao?, ssaoRadius?, ssaoIntensity?, ambientColor?, debugView?}`, `renderer.stats`, `log.get {since?, level?, limit?}`, `log.clear` |

Security: the server only listens on the loopback interface, but **any local process can control the editor**, including writing files through `scene.save`, `editor.screenshot` and `project.export`. Use `--no-server` when that is not wanted. Scripts run sandboxed: `io`, `os`, `debug`, `dofile`, `loadfile` and native modules are not available.

`entity` arguments accept an entity ID (number) or name (string). Commands act on the running scene in play mode and on the edited scene otherwise. Changes made in play mode are discarded by `editor.stop`.

Typical loop: `editor.state` → inspect with `entity.list`/`entity.get` → change with `component.set` → check with `editor.screenshot` (read the PNG) → `scene.save`. Then `editor.play` + `input.tap` + `script.exec` + `game.ui` to test gameplay, and `log.get {"level": "error"}` to check for errors.

## Extending the engine — checklists

**New component**
1. Struct in `Scene/Components.h` (public PascalCase fields with defaults).
2. `to_json`/`from_json` in `Scene/ComponentSerialization.{h,cpp}`. Use `ReadField` so partial patches work.
3. Register it in `Scene/ComponentRegistry.cpp`.
4. Add a Lua proxy in `Scripting/ScriptGlue.cpp` (`RegisterComponents`, plus an entity property in `RegisterEntity`).
5. Draw it in `OuroborosEditor/src/Panels/InspectorPanel.cpp`.
6. If a system uses it (physics, audio, renderer), sync it from the registry like the existing systems do.
7. Tests: `SceneTests.cpp` (round trip in `CreateFullEntity`), `FeatureTest.oscene` + `FeatureTest.lua`, and update the component count checks.

**New script function:** add it to `ScriptGlue.cpp`, raise errors with `throw std::runtime_error(...)` (it becomes a Lua error), cover it in `FeatureTest.lua`, and document it above.

**New agent command:** register it in `SceneCommands.cpp` (if it only needs a scene) or `EditorLayer::RegisterCommands`, give it a description and argument summary, throw `CommandError` for user errors, test it in `AutomationTests.cpp` and/or `EditorAutomation.jsonl`, and add it to the table above.

**New shader or pass:** add `Name.<vs|ps|cs>.hlsl` (it is picked up automatically), keep C++ mirrors of constant buffers in `Renderer/ShaderTypes.h` in sync with `Common.hlsli`, and render the pass in `GpuTests` with validation on.

## Before committing

Follow the `code-review` skill (`.claude/skills/code-review/SKILL.md`):
- Debug and Release build with zero warnings.
- `ctest` all green.
- Style matches this document.
- The diff has been reviewed for correctness, error handling and tests.
- Commit messages are imperative and describe *why*.
