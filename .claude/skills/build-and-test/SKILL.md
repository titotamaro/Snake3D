---
name: build-and-test
description: Configure, build and test the Ouroboros engine, editor, runtime and Snake game (Windows/Linux/macOS), and interpret test failures. Use before and after every code change.
---

# Build and test Ouroboros

## Configure and build

Windows (Visual Studio 2022). Use Visual Studio's bundled CMake, and run commands from **PowerShell**, because Git Bash rewrites `/m`:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --preset windows
& $cmake --build --preset windows-debug -- /m /v:minimal
& $cmake --build --preset windows-release -- /m /v:minimal
```

Linux/macOS: `cmake --preset ninja && cmake --build --preset ninja-debug`.

To show only problems, pipe the build through `Select-String -Pattern "error|warning C"`. Warnings are errors.

## Test

```powershell
$ctest = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
& $ctest --preset windows-debug                         # all tests
& $ctest --preset windows-debug -L unit                 # fast CPU tests
& $ctest --preset windows-debug -LE gpu                 # no GPU available
build\windows\bin\Debug\OuroborosTests.exe -tc="*glTF*" # one doctest case (wildcards)
build\windows\bin\Debug\OuroborosTests.exe -ts=Scripting # one suite
```

Integration tests run the real executables headlessly (deterministic 1/60 s frames):

```powershell
build\windows\bin\Debug\OuroborosRuntime.exe --project SnakeGame\Snake.oproj --scene Scenes/FeatureTest.oscene --headless --frames 600 --test --validation --no-audio
build\windows\bin\Debug\OuroborosEditor.exe --project SnakeGame\Snake.oproj --headless --no-server --exec OuroborosTests\Data\EditorAutomation.jsonl --exit-after-exec
```

`--test` exits with code 1 if any script error, logged error or Vulkan/NVRHI validation error occurred. The log names the cause: `FAILED: ...` lines from `FeatureTest.lua`, or `[Script] ...`, `Vulkan: ...` and `NVRHI: ...` messages.

## Look at the result

Render a frame to a PNG and inspect it with the Read tool:

```powershell
build\windows\bin\Debug\OuroborosRuntime.exe --project SnakeGame\Snake.oproj --headless --frames 30 --screenshot $env:TEMP\shot.png --no-audio
```

`--debug-view 1..5` shows albedo, normals, ambient occlusion, shadow cascades or metallic/roughness.

## After changing assets or shaders

- Regenerate procedural assets with `python Tools/GenerateAssets.py`. The output is deterministic, so commit what it produces.
- Shaders in `Ouroboros/Resources/Shaders` are compiled by the build; a new file is picked up on the next configure.
