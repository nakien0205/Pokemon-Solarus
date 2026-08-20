# Unreal Engine 5.8.1 — Relevant Deprecations

**Last verified:** 2026-08-19

Keep this list narrow. Add an entry only when an official UE 5.8 source and an
approved project task make it relevant.

| Do not introduce | Current direction | Relevance |
|---|---|---|
| `FInputDeviceScope` | `FInputDeviceRegistry` | UE 5.8 officially deprecates the former. Most project input should remain above this low-level layer. |

No project source exists yet, so there are no deprecated project API usages to
migrate.

## Official Source

- [UE 5.8 release notes — Gameplay/Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes)

