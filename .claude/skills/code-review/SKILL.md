---
name: code-review
description: Review a change in the Ouroboros repository before committing — correctness, error handling, style, tests, and a full build/test run. Use before every commit and when asked to review code.
---

# Code review before committing

Work through every item. Do not commit until all of them hold.

## 1. Read the whole diff

Run `git diff` and `git diff --cached`, and check new files with `git status`. For each change, check:

- **Correctness:** off-by-one errors and null or empty inputs. Entities destroyed during callbacks: iterate over copies of ID lists. References invalidated by registry changes. GPU resource lifetimes: `waitForIdle` before destroying resources the GPU may still be using. Thread safety between the TCP server thread and the main thread.
- **Error handling:** bad scenes, scripts, assets and commands produce errors, never crashes. A failed operation leaves state unchanged, like the component patch and the `entity.create` rollback.
- **API contracts:** header comments still describe what the code does.

## 2. Style (see AGENTS.md)

- Hazel naming: `PascalCase` types and functions, `camelCase` locals and parameters, `m_`/`s_` prefixes, `OB_` macros.
- Tabs, Allman braces, and everything inside `namespace Ouroboros {` indented.
- `#include "obpch.h"` comes first in engine `.cpp` files, followed by the file's own header.
- No commented-out code or stray debug logging. Comments explain *why*.

## 3. Tests

- New behavior has tests: unit tests (`OuroborosTests/src`), the feature scene (`FeatureTest.lua`) for components and scripting, and automation data for commands.
- Run the full suite: Debug and Release builds with zero warnings, then `ctest --preset windows-debug` all green (see the `build-and-test` skill).
- For rendering changes, render a screenshot and look at it.

## 4. Commit

- Stage only the relevant files; never `build/`, `Exports/` or `SnakeGame/.ouroboros/`.
- Write an imperative subject line (72 characters at most) and a body that explains why.
