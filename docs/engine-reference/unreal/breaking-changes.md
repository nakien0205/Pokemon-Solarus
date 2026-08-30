# Unreal Engine 5.8.1 — Relevant Changes and Cautions

**Last verified:** 2026-08-19

`Game/PokemonSolarus.uproject` and live C++ project code now exist. This file
records only verified UE 5.8 changes that may affect the approved prototype; it
does not claim that an engine migration is currently required. Earlier generic
version notes are not active project guidance and remain recoverable through
Git history.

## Input Changes Relevant to This Project

- UE 5.8 release notes describe a unified Enhanced Input and Common Input/UI
  workflow intended to reduce duplicate input data and improve debugging.
- UE 5.8 deprecates `FInputDeviceScope` in favor of the thread-safe
  `FInputDeviceRegistry`. The first-battle prototype should not build a custom
  low-level device layer unless the normal engine facilities prove insufficient.
- UE 5.8 includes Enhanced Input fixes around rebuilt mappings, held keys,
  flushed input, and UMG input registration. Test these behaviors when the real
  input implementation exists; do not assume old-version workarounds are needed.

## Toolchain Change

For UE 5.8, Epic documents Visual Studio 2022 17.14 or later as supported, with
MSVC 14.38 and Windows SDK 10.0.22621.0 as minimum versions. The installed
toolchain was checked before pinning; no new toolchain change is authorized by
this document.

## 5.8.1 Hotfix

Epic's 5.8 release topic contains the 5.8.1 hotfix notes. Treat the installed
5.8.1 build as the source of truth and check those notes when a matching engine
bug is suspected.

## Official Sources

- [UE 5.8 release notes](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes)
- [UE 5.8 release and 5.8.1 hotfix notes](https://forums.unrealengine.com/t/unreal-engine-5-8-released/2729274)
- [Visual Studio setup for UE 5.8](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine)
