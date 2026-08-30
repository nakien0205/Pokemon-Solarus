# Unreal Engine 5.8.1 — Project Version Reference

**Last verified:** 2026-08-19

| Field | Value |
|---|---|
| **Engine Version** | Unreal Engine 5.8.1 |
| **Build Changelist** | 56057345 |
| **Compatible Changelist** | 55116800 |
| **Branch** | `++UE5+Release-5.8` |
| **Project Pinned** | 2026-08-19 |
| **Target Platform** | Windows PC |
| **LLM Knowledge Risk** | HIGH — UE 5.8.1 is newer than the May 2025 baseline used by the inherited workflow |

## Evidence and Status

- The installed `Build.version` values above were confirmed during the approved
  setup work that preceded this repository update.
- The user confirmed that the disposable Editor, Play In Editor, command-line,
  log-reading, and editor-Python capability tests passed. Do not repeat them as
  setup checks.
- The authoritative project exists at `Game/PokemonSolarus.uproject`; its live
  source and configuration supersede this historical pre-project setup note.
- Do not launch Unreal solely to re-check this setup record.

The project is pinned to the exact installed build, not merely the `5.8` release
family. Do not upgrade or silently retarget it during the first-battle milestone.

## Required Read Order

Before giving Unreal-specific implementation advice:

1. Read `UE.md` for the project scope and learning boundary.
2. Read this file for the exact engine build.
3. Read only the active module note relevant to the task.
4. Verify uncertain or version-sensitive details against Epic's UE 5.8
   documentation and release notes.

## First-Battle Boundary

The approved milestone is one polished, single-player battle between two
Pokémon. GAS, replication, full CommonUI architecture, advanced rendering,
and unrelated Unreal systems are not approved for this milestone.

## Official Sources

- [Unreal Engine 5.8 documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-documentation)
- [Unreal Engine 5.8 release notes](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes)
- [UE 5.8 release and 5.8.1 hotfix notes](https://forums.unrealengine.com/t/unreal-engine-5-8-released/2729274)
- [Visual Studio setup and UE 5.8 compatibility](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine)
