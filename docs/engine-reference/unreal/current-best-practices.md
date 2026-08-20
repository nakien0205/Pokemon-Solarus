# Unreal Engine 5.8.1 — First-Battle Practices

**Last verified:** 2026-08-19

This is a deliberately small project reference. It is not a general Unreal
architecture guide.

## Approved Defaults

- Use C++ for battle rules, state, and code that benefits from automated tests.
- Use Blueprint for rapid prototyping, content variation, presentation hooks,
  and visual assembly. Blueprint is not the primary gameplay language.
- Build C++ through Unreal Build Tool (UBT).
- Use Unreal's normal content import pipeline for sprites, arena assets, audio,
  and effects.
- Use Paper 2D sprites and Flipbooks for the two animated Pokémon in the 3D
  arena. Tile Maps are not needed for this battle.
- Use basic UMG for the move menu and HP display. Do not adopt a complete
  CommonUI screen-stack architecture for this prototype.
- Use one ordinary fixed Camera Actor before considering camera systems or
  cinematic tooling.
- Choose between Data Assets and Data Tables only after the two-Pokémon and
  four-move data fields are defined. Do not implement both by default.
- Use ordinary Sound Wave playback and small reusable Niagara effects only
  after the battle loop works.

## Input

Use Enhanced Input concepts for gameplay actions and mapping contexts. The
complete approved contract, including keyboard and controller mappings, prompt
switching, direction conflict handling, and future-scope actions, is in
`modules/input.md`.

UE 5.8 unified parts of Enhanced Input and Common Input/UI. That makes its
official documentation relevant to prompt and device handling, but it does not
by itself approve full CommonUI architecture for this project.

## Avoid Premature Systems

Do not introduce these during the first-battle milestone without a new,
specific approval:

- Gameplay Ability System;
- replication, online subsystems, or multiplayer;
- full CommonUI architecture, MVVM, or Slate;
- Gameplay Cameras or Sequencer-heavy camera architecture;
- PCG, World Partition, Mass Entity, or open-world systems;
- Substrate migration, MegaLights, ray tracing, or custom rendering systems;
- Asset Manager and Primary Asset rules;
- MetaSounds, Niagara Fluids, or advanced procedural audio/VFX;
- runtime Python gameplay.

## Official Sources

- [Programming with C++](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-with-cplusplus-in-unreal-engine)
- [Exposing C++ to Blueprints](https://dev.epicgames.com/documentation/unreal-engine/exposing-cplusplus-to-blueprints-visual-scripting-in-unreal-engine)
- [2D and Paper 2D](https://dev.epicgames.com/documentation/en-us/unreal-engine/2d-in-unreal-engine)
- [UMG UI Designer quick start](https://dev.epicgames.com/documentation/unreal-engine/umg-ui-designer-quick-start-guide-in-unreal-engine)
- [Data Assets](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-assets-in-unreal-engine)
- [Data-driven gameplay elements](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine)
- [Camera Actors](https://dev.epicgames.com/documentation/en-us/unreal-engine/camera-actors-in-unreal-engine)
- [Enhanced Input](https://dev.epicgames.com/documentation/unreal-engine/enhanced-input-in-unreal-engine)
- [UE 5.8 release notes](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes)

