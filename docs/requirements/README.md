# Requirements documents

Baseline requirements for Snake 3D and the Ouroboros engine (v1.0, 21 Oct 2026).

| Document | What it covers |
| --- | --- |
| [BRD.md](BRD.md) | Business Requirements: objectives, scope, stakeholders, business requirements (BR-xx), risks |
| [FSD.md](FSD.md) | Functional Specification: game flow, gameplay rules, controls, HUD, toolchain functions, use cases, acceptance criteria |
| [SRS.md](SRS.md) | Software Requirements: architecture, functional (SR-xx) and non-functional (NFR-xx) requirements, interfaces, verification |

Requirements trace top-down: BR-xx in the BRD → FSD sections → SR-xx / NFR-xx in the SRS → the `ctest` suites listed in SRS §8.
