# Snake 3D — Business Requirements Document (BRD)

Oct 21, 2026 · Tito Tamaro

## 1. Document control

This BRD defines the business needs that Snake 3D and its Ouroboros engine must satisfy. It was written retrospectively from the shipped codebase ([titotamaro/Snake3D](https://github.com/titotamaro/Snake3D), commit 26f97f6, 16 Oct 2026), so it doubles as the baseline for future changes.

| Item | Value |
| --- | --- |
| Product | Snake 3D (game) on the Ouroboros 3D engine |
| Document version | 1.0 |
| Status | Draft for review |
| Related documents | [Snake 3D — Functional Specification (FSD)](FSD.md); [Snake 3D — Software Requirements Specification (SRS)](SRS.md) |
| Intended readers | Product owner, developers, QA, AI development agents, future contributors |

**Revision history**

| Version | Date | Author | Change |
| --- | --- | --- | --- |
| 1.0 | 21 Oct 2026 | Tito Tamaro | Initial baseline from repository analysis |

## 2. Executive summary

The business needs a polished, stable 3D Snake game for Windows, macOS and Linux, plus an in-house engine that can build and ship it without third-party engine licensing. The game is the showcase product; the engine is the reusable asset.

**Background.** The project began as an initiative to prove that a production-grade game engine and game can be built with heavy use of AI development agents. Three non-negotiable themes came from the original brief:

- **Quality over speed:** "production-grade, no hacks", with automated testing for every feature.
- **Own the technology:** a custom engine (Ouroboros) on open-source libraries (Vulkan via NVRHI, Jolt, Lua, miniaudio) rather than Unity or Unreal.
- **Agent-operable tooling:** the editor must be fully controllable by AI agents, so future content and features can be produced faster.

**Problem being solved.** Commercial engines carry licensing cost, large footprints and limited automation hooks. A small team needs a compact engine it understands end to end, that ships a standalone game, and that both humans and agents can drive.

## 3. Business objectives and success metrics

Success means a player can download one folder, run it on any of the three desktop platforms, and play a complete, crash-free Snake game.

| ID | Objective | Success metric | Status at baseline |
| --- | --- | --- | --- |
| BO-01 | Deliver an engaging, complete casual game | Full loop playable: start → play → pause → game over / win → restart, with score and best score | Met |
| BO-02 | Reach all major desktop platforms | Builds from one codebase on Windows, Linux and macOS | Met (Windows, Linux presets; macOS via Ninja) |
| BO-03 | Ship a standalone product | One-click export produces a runnable game with no editor | Met |
| BO-04 | Production-grade stability | Zero logged errors and zero Vulkan validation errors in automated runs; warnings treated as errors | Met |
| BO-05 | Build a reusable engine asset | Engine, editor and runtime are separate modules; game logic lives only in data and Lua scripts | Met |
| BO-06 | Enable AI-assisted production | Every editor action available over a local JSON API and CLI | Met |
| BO-07 | Keep visual quality competitive | Modern PBR lighting, soft shadows, SSAO and HDR tonemapping | Met |
| BO-08 | Avoid licensing cost and risk | Only permissive open-source dependencies and CC0 / self-generated assets | Met |

## 4. Scope

Release 1.0 covers a single-player, single-level desktop game and the engine toolchain that produces it.

**In scope**

- Snake 3D game: 20 × 20 board, keyboard controls, scoring, session best score, progressive speed, win/lose states, HUD, music and sound effects, physics-driven crash effect.
- Ouroboros engine: rendering, ECS scenes, physics, audio, Lua scripting, asset import (glTF, images, HDRI).
- Ouroboros Studio editor: scene editing, gizmos, play mode, game export.
- Ouroboros Runtime: the player shipped with exported games.
- Agent automation API and command-line client.
- Automated test suite and procedural asset generator.

**Out of scope (Release 1.0)**

- Online features: multiplayer, leaderboards, accounts, analytics.
- Persistent saves: the best score resets when the game closes.
- Mobile, web and console platforms.
- Gamepad and touch input; key remapping.
- Settings menu (volume, resolution, graphics quality) inside the game.
- Monetization, localization and accessibility options.
- Installer packages and store distribution.

## 5. Stakeholders and target users

The product serves two audiences: players who want a quick, satisfying casual game, and the team (human and AI) that builds games with the engine.

| Stakeholder | Role | Primary interest |
| --- | --- | --- |
| Product owner (Tito Tamaro) | Sponsor, final approver | Quality, delivery, reuse of the engine |
| Players | End users | Fun, responsive controls, smooth visuals, no crashes |
| Game developers / designers | Build content in the editor | Fast iteration, clear scripting API |
| Engine developers | Extend Ouroboros | Clean architecture, code style, tests |
| AI development agents | Automated builders and testers | Complete, documented JSON API; deterministic headless runs |
| QA | Verify releases | Automated suites, reproducible results |

**Player persona.** A casual desktop player, any age, who knows classic Snake and expects to start in seconds with no tutorial. Sessions last 1–10 minutes. Owns a mid-range PC with a Vulkan 1.3 GPU.

## 6. Business requirements

Fourteen business requirements define Release 1.0; each traces to an objective and is detailed in the FSD and SRS. Priority uses MoSCoW (Must / Should / Could).

| ID | Requirement | Priority | Objective | Detailed in |
| --- | --- | --- | --- | --- |
| BR-01 | Players can play classic Snake on a 3D board with keyboard steering. | Must | BO-01 | FSD §4.1–4.3 |
| BR-02 | The game tracks score and the best score of the session and shows them on screen. | Must | BO-01 | FSD §4.4, §4.7 |
| BR-03 | Difficulty rises as the player scores, up to a fixed maximum speed. | Must | BO-01 | FSD §4.5 |
| BR-04 | Players can pause, resume and restart without relaunching. | Must | BO-01 | FSD §4.6 |
| BR-05 | The game gives clear audio-visual feedback: music, turn/eat/crash sounds, crash effect. | Should | BO-01, BO-07 | FSD §4.8 |
| BR-06 | The game runs on Windows, Linux and macOS from one codebase. | Must | BO-02 | SRS §5 |
| BR-07 | The team can export a standalone, non-editable build. | Must | BO-03 | FSD §5.4 |
| BR-08 | The product runs without crashes or logged errors under automated test. | Must | BO-04 | SRS §6, §8 |
| BR-09 | Game rules are separated from engine code and unit tested. | Must | BO-04, BO-05 | SRS §4 |
| BR-10 | Designers can build scenes visually in an editor with play-in-editor. | Must | BO-05 | FSD §5 |
| BR-11 | Game behaviour is authored in a scripting language, not C++. | Must | BO-05 | SRS §4.6 |
| BR-12 | AI agents can drive every editor function through a documented local API. | Must | BO-06 | FSD §5.5 |
| BR-13 | Rendering quality includes PBR, image-based lighting, soft shadows, SSAO and HDR. | Should | BO-07 | SRS §4.3 |
| BR-14 | All third-party code and assets are permissively licensed or self-generated. | Must | BO-08 | SRS §9 |

## 7. Assumptions, constraints, dependencies and risks

The largest business risk is hardware reach: Vulkan 1.3 is required, which excludes older GPUs and some Mac setups.

**Assumptions**

- Players have a desktop PC with keyboard and a Vulkan 1.3-capable GPU and driver.
- Distribution is a zipped folder from the export function; no installer is needed for Release 1.0.
- A session-only best score is acceptable for a first release.

**Constraints**

- Technology stack is fixed by the brief: C++20, CMake, GLFW, NVRHI on Vulkan, glm, miniaudio, Lua.
- Code style follows the Hazel engine conventions.
- Every change must pass the automated test suite before commit; warnings are errors.

**Dependencies**

- Vulkan SDK (for the DXC shader compiler) on build machines.
- Open-source libraries fetched at pinned versions at build time.
- Poly Haven HDRI (CC0) for sky lighting.

**Risks**

| ID | Risk | Impact | Likelihood | Mitigation |
| --- | --- | --- | --- | --- |
| R-01 | Players lack Vulkan 1.3 hardware (older GPUs; macOS needs MoltenVK) | High | Medium | Publish minimum specs; consider a fallback back end later |
| R-02 | The editor API lets any local process change files | Medium | Low | Loopback only; `--no-server` flag; runtime has no server |
| R-03 | Best score is lost on exit, lowering replay value | Low | High | Add local save in a later release |
| R-04 | Pinned dependency downloads fail on build machines | Medium | Low | Pinned versions; documented toolchain notes |
| R-05 | Single-level content limits engagement | Medium | Medium | Engine supports more scenes; plan modes/levels in roadmap |

## 8. Approval

Sign-off confirms the requirements above form the Release 1.0 baseline.

| Role | Name | Signature | Date |
| --- | --- | --- | --- |
| Product owner | Tito Tamaro | | |
| Lead developer | | | |
| QA lead | | | |
