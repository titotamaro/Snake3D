---
name: extend-engine
description: Checklists for adding engine features to Ouroboros — components, Lua scripting API, agent commands, shaders/render passes, physics or audio features — including the tests each change needs.
---

# Extend the Ouroboros engine

Read AGENTS.md first (code style, conventions, architecture). Every feature ships with tests, and the build stays warning-free.

## New component

1. `Ouroboros/src/Ouroboros/Scene/Components.h`: a plain struct with PascalCase fields and sensible defaults.
2. `Scene/ComponentSerialization.{h,cpp}`: `to_json` writes every field. `from_json` uses `ReadField` (so partial patches work) and throws `json::exception` for invalid values such as unknown enum strings.
3. `Scene/ComponentRegistry.cpp`: `MakeComponentInfo<T>("Name")`.
4. `Scripting/ScriptGlue.cpp`: add a `ComponentName<T>` specialization, a proxy usertype in `RegisterComponents` (`OB_PROXY_FIELD`), and a `"Name"` property in `RegisterEntity`.
5. `OuroborosEditor/src/Panels/InspectorPanel.cpp`: a `DrawComponent<T>` block.
6. Systems: if the component affects physics, audio or rendering, read it from the registry each step or frame. `PhysicsWorld::SyncBodies` and `SceneAudio::Update` show how to react to added, removed and changed components.
7. Tests:
   - `OuroborosTests/src/SceneTests.cpp`: set non-default values in `CreateFullEntity`, check the round trip, and update the expected component list.
   - `AutomationTests.cpp`: update the `component.types` count.
   - `SnakeGame/Assets/Scenes/FeatureTest.oscene` and `Scripts/Tests/FeatureTest.lua`: use every field and method from Lua.
8. Document it in AGENTS.md.

## New Lua API

- Add it to `ScriptGlue.cpp` in the matching `Register*` function. Report invalid input with `throw std::runtime_error("...")`, which sol2 turns into a Lua error.
- Use `LuaEntity` (UUID based) for entities; never hand out raw component references.
- Cover it in `FeatureTest.lua`, and in `ScriptingTests.cpp` for edge cases. Document it in AGENTS.md.

## New agent command

- Scene-level commands go in `Automation/SceneCommands.cpp`, editor-level ones in `EditorLayer::RegisterCommands`.
- Register with `processor.Register(name, description, argsSummary, handler)`. Use `CommandProcessor::RequireArgument` and throw `CommandError` for user errors. Commands that need frames to pass return `CommandResult::Deferred(poll)`.
- Test it in `AutomationTests.cpp` and/or `OuroborosTests/Data/EditorAutomation.jsonl`, and add it to the AGENTS.md table.

## New shader or render pass

- Add `Ouroboros/Resources/Shaders/<Name>.<vs|ps|cs>.hlsl`, then re-run configure so the file is compiled.
- Register shifts follow NVRHI's defaults (t0+, s128+, b256+, u384+). When a pipeline uses several binding layouts, use `setRegisterSpaceAndDescriptorSet(n)` matching HLSL `spaceN`.
- Clip space is Y up and UV (0,0) is the top-left corner; use `UVToClip`/`ClipToUV` from `Common.hlsli`.
- Keep `Renderer/ShaderTypes.h` byte-compatible with the HLSL structs, with `static_assert`s on the sizes.
- Create command lists with `setEnableImmediateExecution(false)`.
- Add coverage to `RendererTests.cpp` (GPU suite) and keep the validation error count at 0.

## Physics and audio

- Physics uses Jolt through `Physics/PhysicsWorld`. Bodies mirror the components every fixed step; contact events are queued and dispatched to scripts after the step. Test with a standalone `PhysicsWorld` in `PhysicsTests.cpp`.
- Audio uses miniaudio through `Audio/AudioEngine` (global) and `SceneAudio` (one per running scene). Tests run with `AudioEngine::Init(true)`, which needs no audio device.
