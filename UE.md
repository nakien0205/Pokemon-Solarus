# Unreal Engine Reference for the First Battle Prototype

> **Purpose:** a small, project-specific learning map for a solo beginner.
> It is not a complete Unreal Engine manual and it does not approve an engine
> installation or a game architecture.

**Last researched:** 2026-08-19  
**Project engine version:** Unreal Engine 5.8.1, changelist 56057345  
**Current Epic release/documentation checked:** Unreal Engine 5.8 documentation and 5.8.1 hotfix notes

## How to Use This File

Unreal's documentation covers everything from tiny games to large studios. That
does not mean our project needs everything it describes.

Use these rules:

1. Read only the topic needed for the current task.
2. Use documentation matching the engine version actually installed.
3. Stop a tutorial once the relevant skill is learned; do not copy its sample
   game's architecture.
4. Treat Beta and Experimental features as unavailable unless we approve a
   specific reason to use one.
5. Add a system only when it makes the current prototype simpler or solves a
   measured problem.

Do not copy entire online guides into this repository. This file summarizes the
useful parts and links to their original sources so they can be checked for
updates.

## Scope Filter

Our current goal is one polished, single-player battle with two Pokemon. The
prototype needs only:

- one simple 3D battle arena and a fixed camera;
- two animated 2D Pokemon sprites;
- a turn-based battle loop with four moves;
- HP, stats, types, accuracy, damage, turn order, and fainting;
- a basic battle menu and HP display;
- a few reusable effects and placeholder sounds.

It does **not** currently need an overworld, multiplayer, saving, inventory,
quests, procedural worlds, or a production content pipeline.

## Reference Layout

The active files under `docs/engine-reference/unreal/` are lean, project-scoped
UE 5.8.1 references. The former generic UE 5.7 material is preserved under
`docs/engine-reference/unreal/legacy-generic-5.7/` for historical context only.
Do not treat the legacy folder as active guidance.

This file remains the project-specific entry point. If active guidance is
unclear, verify the issue against the pinned build and Epic's UE 5.8 sources.

## What Codex Can and Cannot Do with Unreal

Full Access is a permission level, not an Unreal integration.

| Work | Practical method |
|---|---|
| Documentation, plans, C++, configuration, and scripts | Codex can edit these as text. |
| Builds, command-line launches, logs, and automated checks | Codex can run these when the engine is installed and the task has permission. |
| Blueprints, levels, UMG widgets, materials, and Niagara assets | These are mainly binary `.uasset` or `.umap` files and must be changed through Unreal Editor or editor automation. |
| Visual layout and art judgment | Best handled collaboratively in the Editor with screenshots and playtests. |
| Epic account login and installer security prompts | The user should handle these. |

The Codex desktop app can interact with visible Windows applications when that
capability and permission are available. This may help with Unreal Editor, but
it is less reliable than text editing and command-line automation. We should not
assume every editor action can be automated.

Unreal also supports editor scripting with Python. Epic states that this Python
environment is for editor and asset-pipeline automation, not gameplay runtime.
That matches this project's intended use of Python.

## Version and Installation Policy

Unreal Engine 5.8.1 is installed and pinned for this project:

- build changelist: `56057345`;
- compatible changelist: `55116800`;
- branch: `++UE5+Release-5.8`.

The disposable capability tests passed. Do not repeat them as setup checks.
Once pinned, do not upgrade during the first-battle milestone unless a blocking
engine bug is verified and the user separately approves the change.

Pre-install local readiness was checked on 2026-08-18:

- Windows 11, Intel i7-13620H, and RTX 4060 Laptop GPU with about 8 GB VRAM;
- about 16 GB system RAM, below Epic's 32 GB recommendation;
- Visual Studio 2022 Community 17.14.10, which meets Epic's documented UE 5.8
  minimum for Visual Studio 2022;
- MSVC v143 and a Windows 11 SDK are present;
- the Visual Studio workload state was part of the pre-install check and is not
  re-audited by this repository update;
- enough free disk space was visible, but the Epic Games Launcher must show the
  real installation size for the selected components before installation.

The machine appears suitable for a small prototype, but 16 GB RAM may make the
Editor, shader compilation, and multitasking slower. That is an inference, not
a performance guarantee.

Official setup references:

- [Install Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/install-unreal-engine?lang=en-US)
- [Hardware and software specifications](https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-and-software-specifications-for-unreal-engine)
- [Visual Studio setup for C++ projects](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine)
- [UE 5.8 release notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes)
- [UE 5.8 release and 5.8.1 hotfix notes](https://forums.unrealengine.com/t/unreal-engine-5-8-released/2729274)

The real `.uproject` has not been created. It requires separate approval.

## Input Contract

The Windows PC build supports keyboard and compatible Xbox, PlayStation, and
Nintendo controllers connected to the PC. This is controller support on PC,
not a console-release target. Mouse and touch gameplay are unsupported.

Prompts follow the most recent meaningful keyboard or gamepad input. Recognized
controllers show matching glyphs; unknown controllers use generic gamepad
prompts. The exact mappings and conflict rules live in
`docs/engine-reference/unreal/modules/input.md`.

## Minimal Learning Path

### 1. Learn the Editor, Then Stop

Learn how to open a project, move around the viewport, find assets in the
Content Browser, edit properties in the Details panel, save, and use Play In
Editor. Do not complete a large sample game just to learn these controls.

- [Unreal Engine for New Users](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-for-new-users)
- [Unreal Editor interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface)
- [Create your first project](https://dev.epicgames.com/documentation/en-us/unreal-engine/create-your-first-project-in-unreal-engine)

### 2. Learn Only Basic Blueprints

A Blueprint is Unreal's visual scripting asset. For the first learning exercise,
learn variables, functions, events, branches, arrays, structs, enums, and object
references. Avoid large Event Graphs and avoid building a general framework.

- [Blueprints overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-blueprints-visual-scripting-in-unreal-engine)
- [Introduction to Blueprints](https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-blueprints-visual-scripting-in-unreal-engine)

Blueprints are acceptable for the first prototype. Move a small piece to C++
only when it makes battle rules easier to understand, validate, or test. Epic's
own guidance supports combining the two based on complexity and performance.

- [Combining C++ with Blueprints](https://dev.epicgames.com/documentation/unreal-engine/exposing-cplusplus-to-blueprints-visual-scripting-in-unreal-engine)

### 3. Learn Paper 2D Sprites and Flipbooks

Paper 2D is Unreal's sprite system for 2D and 2D/3D hybrid games. Learn sprite
import, one basic Flipbook animation, transparency, filtering, pivots, scale, and
how a lit or unlit sprite material changes the look in a 3D scene.

Do not learn Tile Maps for the battle prototype; the environment is 3D.

- [2D and Paper 2D overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/2d-in-unreal-engine)
- [Paper 2D Flipbooks](https://dev.epicgames.com/documentation/unreal-engine/paper-2d-flipbooks-in-unreal-engine?lang=en-US)
- [Paper 2D sprite materials](https://dev.epicgames.com/documentation/unreal-engine/paper-2d-sprite-material-in-unreal-engine)

For characters, we should test controlled rotation toward the battle camera.
Do not assume Unreal's generic Billboard Component is the final solution; it
always faces the camera and may remove the deliberate angle that gives an
HD-2D scene depth.

### 4. Learn Basic UMG UI

UMG is Unreal's visual user-interface system. Learn one Widget Blueprint with
text, buttons, a progress bar, layout containers, anchors, and keyboard focus.
That is enough for the first move menu and HP display.

- [UMG UI Designer quick start](https://dev.epicgames.com/documentation/unreal-engine/umg-ui-designer-quick-start-guide-in-unreal-engine)

The linked tutorial uses a first-person example. Learn the widget operations;
do not copy its character or HUD architecture.

### 5. Compare Data Assets and Data Tables at the Decision Point

Both systems store gameplay data. Do not implement both automatically.

- A **Data Asset** is one editable asset instance, which may suit Pokemon and
  moves that reference sprites, sounds, and VFX.
- A **Data Table** stores many rows with the same structure and supports CSV
  import, which may suit future bulk data tooling.

We will choose the smallest option after defining the two-Pokemon data fields.

- [Data Assets](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-assets-in-unreal-engine)
- [Data-driven gameplay and Data Tables](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine)

### 6. Use One Ordinary Camera

Learn a normal Camera Actor, field of view, transform, and switching the active
view. A fixed battle camera is enough initially. Camera movement can be added
only after the static composition works.

- [Camera Actors](https://dev.epicgames.com/documentation/en-us/unreal-engine/camera-actors-in-unreal-engine)

Do not adopt the Experimental Gameplay Cameras system.

### 7. Add Minimal VFX and Audio Last

For VFX, learn how to make or modify one simple Niagara system and expose a few
parameters for reuse. Do not use Niagara Fluids or write custom Niagara modules.

- [Getting started in Niagara](https://dev.epicgames.com/documentation/en-us/unreal-engine/getting-started-in-niagara-effects-for-unreal-engine)

For audio, import placeholder Sound Waves and use simple Play Sound calls. Do
not learn MetaSounds until ordinary playback is insufficient.

- [Audio Engine overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-engine-overview-in-unreal-engine)

### 8. Package Only After the Battle Works in the Editor

Packaging turns the project into a standalone Windows executable. Learn it when
the complete battle can be played from start to finish in the Editor.

- [Packaging a project](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project)

## Ignore for the First Battle

Do not study or adopt these yet:

- Gameplay Ability System (GAS);
- multiplayer, replication, online subsystems, or dedicated servers;
- CommonUI, UMG Viewmodel/MVVM, or Slate;
- World Partition, PCG, Mass Entity, or large-world tools;
- Gameplay Cameras, Sequencer-heavy camera architecture, or cinematic pipelines;
- Substrate migration, MegaLights, ray tracing, or custom rendering systems;
- Niagara Fluids or advanced GPU simulations;
- MetaSounds or procedural audio;
- Control Rig, IK Rig, Motion Matching, or 3D character rigs;
- Chaos destruction, vehicles, or complex physics;
- Asset Manager, asynchronous streaming, or Primary Asset rules unless the
  small prototype demonstrates a real loading problem;
- the Experimental UE 5.8 MCP plugin as a project dependency.

Lumen and Nanite may be enabled by a project template. We do not need to learn,
tune, or build around them for the first battle unless a measured visual or
performance problem appears.

## Python Later, Not Gameplay Runtime

Python may later help with data conversion, validation, sprite checks, imports,
and batch asset operations. It should not contain battle gameplay logic.

- [Scripting the Unreal Editor with Python](https://dev.epicgames.com/documentation/unreal-engine/scripting-the-unreal-editor-using-python)
- [Scripting and automating the Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-and-automating-the-unreal-editor)

## Completed Disposable Capability Check

Before this pin was recorded, a disposable scratch project was used to test:

1. Unreal Editor opens and remains stable on this machine.
2. The user can navigate, save a level, and run Play In Editor.
3. Codex can locate and launch the installed Editor from the terminal when
   permission is available.
4. Codex can read logs and run a harmless command-line or editor-Python check.
5. We can identify which visual Editor steps are reliable for Codex and which
   are easier for the user to perform with click-by-click guidance.

The user confirmed that these checks passed. They authorize the engine pin, but
they do not authorize creation of the real `.uproject`.

## Small Glossary

- **Project:** the whole game workspace, represented by a `.uproject` file.
- **Level / Map:** a scene containing the arena, camera, lights, and Actors.
- **Actor:** an object that can exist in a Level.
- **Component:** a reusable capability attached to an Actor, such as a camera or
  sprite renderer.
- **Blueprint:** a visual class or script saved as an Unreal asset.
- **UMG:** Unreal's system for menus and HUD widgets.
- **Paper 2D:** Unreal's sprite and flipbook system.
- **Niagara:** Unreal's visual-effects system.
- **Play In Editor (PIE):** running the game inside the Editor for testing.

## Source Trust Order

When this file does not answer a question:

1. Check the installed engine version.
2. Use Epic documentation for that version.
3. Use Epic sample projects or official learning material.
4. Use a community guide only when it states its UE version and we can verify
   its important claims against current engine behavior or official docs.
5. Test the smallest possible example before applying advice to the real game.

This order avoids both outdated instructions and unnecessary studio-scale
architecture.
