# ADR-0001: Data-Driven Battle Runtime and Fail-Closed HUD Presentation

## Status

Accepted

## Date

2026-08-24

## Engine Compatibility

| Field | Value |
|-------|-------|
| **Engine** | Unreal Engine 5.8.1 |
| **Domain** | Core / UI |
| **Knowledge Risk** | HIGH — the pinned engine version post-dates the configured knowledge cutoff |
| **References Consulted** | `docs/engine-reference/unreal/VERSION.md`, `docs/engine-reference/unreal/breaking-changes.md`, `docs/engine-reference/unreal/deprecated-apis.md` |
| **Post-Cutoff APIs Used** | None. The design uses established `UDataTable`, `TSoftObjectPtr`, `UUserWidget`, `AddToPlayerScreen`, `IsInViewport`, `NativeConstruct`, and `RemoveFromParent` APIs. |
| **Verification Required** | Focused automation must exercise soft loading, widget construction, `AddToPlayerScreen`, repeated UMG construction, fail-closed presentation, controller replacement, and cooked-asset reachability. A packaged Windows smoke test remains required because PIE alone does not prove cook reachability. |

## ADR Dependencies

| Field | Value |
|-------|-------|
| **Depends On** | None |
| **Enables** | The production Battle HUD handoff and the later full Battle content package |
| **Blocks** | Production acceptance of the Battle runtime presentation seam until this ADR is accepted and implemented |
| **Ordering Note** | Import the approved data assets only after their reflected row structures compile. Remove the duplicate UI-owned setup only after the imported provider reproduces the approved matchup. |

## Context

### Problem Statement

The initial Charizard-versus-Venusaur matchup is approved, but its gameplay catalog, setup, policies, identities, and random seed are duplicated in `Private/UI`. The Battle controller also adds the root HUD before authoritative names, HP, and command state have been validated. The GameMode presentation cache remembers only a request, so replacing the controller or HUD can suppress delivery to the new view. Existing runtime tests inject class-default objects and manually announce readiness, bypassing the production lifecycle they are intended to prove.

These problems share one boundary failure: production composition, gameplay data, and HUD presentation readiness do not have explicit contracts.

### Constraints

- Preserve the approved single-Battle matchup and deterministic seed.
- Reuse the existing Battle definition rows, `FBattleDataTableAdapter`, setup validation, stat calculation, observer snapshot, and presentation adapter.
- Keep gameplay definitions out of `Private/UI` and avoid species- or move-specific branches in reusable runtime code.
- Treat editable JSON as import source only. A packaged game must read cooked Unreal assets and must not perform loose-file JSON I/O.
- Keep the complete root HUD hidden until all required initial presentation data is valid.
- Do not alter Battle HUD visual assets, layout, styling, animation, or art.
- Preserve the last valid display after a later runtime error while disabling command submission.
- This decision covers the current local single-player controller. It does not define multiplayer authority or ownership.

### Requirements

- Production must supply authoritative display names, player HP, opponent HP, and command availability through one validated HUD state.
- A controller or HUD replacement must receive the current request even when the request identity has not changed.
- Initialization failure must leave the root HUD collapsed and command input unavailable.
- Runtime code must depend on an injected Battle runtime-source interface rather than a concrete UI helper.
- Authored IDs and the deterministic seed must retain their full 64-bit values across JSON import.
- Imported assets must be discoverable by the cooker and covered by an asset dependency check or packaged smoke test.
- Focused tests must cover every unavailable-reason mapping and the actual production presentation lifecycle.

## Decision

### 1. Author Battle data outside UI and ship cooked DataTables

The approved initial dataset is authored as JSON under `Game/SourceData/Battle/Initial/`. An Unreal Editor import script converts that source into `UDataTable` assets under `/Game/Data/Battle/Initial/`. Production code synchronously loads the small, fixed composition root and its soft-referenced tables during Battle startup. Runtime code never reads the source JSON.

The seven existing catalog table families remain authoritative: species/forms, natures, moves, abilities, items, conditions, and type chart. `FBattleDataTableAdapter` converts and validates those reflected rows into the non-reflected Battle definition catalog.

Two additional tables are introduced:

- A localized display-name table keyed by stable definition ID and containing `FText`.
- An initial-runtime table containing one reflected scenario row: table references, snapshot identities, encounter policies, trainers, party inputs, active assignments, obedience inputs, and deterministic seed.

All DataTable rows and nested authored elements are reflected `USTRUCT` types, and top-level rows inherit `FTableRowBase`. Authored fields use `FName`, `FString`, integers, booleans, `FText`, arrays, and soft object references. They do not expose non-reflected core ID or enum types to Unreal reflection.

Numeric Battle, Trainer, battler, source-Pokemon, and seed values are authored as decimal strings, then parsed and range-validated as `uint64`. This avoids JSON numeric precision loss. Definition IDs are authored as stable names and converted with the existing typed-ID factories.

The current scenario authors level 50, 31 IVs in every stat, zero EVs, and neutral `Nature.Hardy`. Permanent stats are derived from species base stats with `FBattleStatCalculator`; the existing calculated values are not duplicated. Both battlers start at full HP, so `CurrentHP` derives from calculated maximum HP. Move maximum PP derives from catalog `BasePP`, current PP starts full, and the initial provider rejects non-zero PP Ups. Supporting boosted PP is deferred until a canonical PP-Up rule is introduced.

The reflected GameMode configuration holds a soft reference to the composition-root table. Imported tables are copied immediately into validated non-UObject runtime values; the plain provider retains neither DataTable row pointers nor UObject ownership. `/Game/Data/Battle/Initial/`, `/Game/UI/Battle/`, and `/Game/Input/` are explicitly included in packaging configuration because the production defaults originate from native soft paths. Packaged Windows validation confirms their reachability.

### 2. Inject a runtime source at the GameMode composition boundary

GameMode depends on `IBattleRuntimeSource`, not on a concrete UI setup helper. The production implementation loads and validates the configured DataTables and returns one runtime bundle containing:

- the created `FBattleEngine`,
- the local `FTrainerId`, and
- an immutable display-name resolver containing copied `FText` values.

Tests may inject a deterministic interface implementation without replacing the production lifecycle under test. The current `FBattleRuntimeSetupSource` implementation in `Private/UI` is removed only after its approved values have been migrated and equivalence-tested.

### 3. Present one atomic, fail-closed HUD state

`FBattlePresentationAdapter` consumes one observer-safe Battle snapshot, the pending decision request, and the display-name resolver to build one `FBattleHUDDisplayState`. That state includes both combatant display names, player and opponent HP projections, the prompt, the four typed command states, and whether command input is allowed.

The HUD validates the entire state and every required child binding before mutating any widget. On initial presentation it remains `Collapsed` through construction, viewport attachment, binding validation, and state application. It becomes visible only after:

1. the configured HUD soft class loads,
2. the widget is created for the local player,
3. `AddToPlayerScreen` succeeds,
4. `IsInViewport` confirms attachment,
5. the native widget lifecycle reports all structural bindings ready, and
6. the complete display state validates and applies successfully.

`NativeConstruct` may execute more than once, so native delegate binding is idempotent. The root is reasserted as collapsed after `Super::NativeConstruct` and after viewport attachment. An initial error removes or keeps the HUD collapsed, disables submission, and reports the error. A later invalid update preserves the last valid visual state but disables command submission until a valid state arrives.

HUD visibility and command-input availability are separate state. Visibility alone never implies that a command may be submitted.

### 4. Make presentation caching view-generation aware

Each successfully created and attached HUD receives a monotonically increasing presentation generation owned by its controller. The GameMode cache key contains both the Battle request identity and that HUD generation. Attaching a different controller clears the remembered presentation and immediately refreshes if that controller already has a ready HUD generation.

The cache is committed only after the full HUD state applies successfully. A changed request always redelivers. The same request and same generation may be deduplicated. The same request on a replacement generation must be delivered.

### 5. Prove the production lifecycle

Focused native tests retain small adapter and mapping tests, but production acceptance also opens `FoundationMap` in automated PIE and observes the real GameMode, PlayerController, soft-loaded `WBP_BattleHUD`, viewport attachment, native construction, authoritative names and HP, command availability, root visibility, and controller facade delegates. Tests do not manually broadcast HUD readiness for this path.

Actual-size visual validation at 1920x1080 and 1280x720 remains a manual Blueprint/PIE acceptance step because native automation cannot prove art, layout, or readability.

Typed Battle-decision submission is not added by this decision. No reviewed C++ consumer currently proves that Blueprint command events submit a typed decision, so that behavior remains explicitly unproven rather than being declared working or broken.

### Architecture Diagram

```text
Editable JSON source
        |
        | Unreal Editor import + validation
        v
Cooked UDataTables ----> BattleDataTableRuntimeSource
                              |  implements
                              v
                       IBattleRuntimeSource
                              |
                 runtime bundle: engine + local trainer
                              + copied display-name resolver
                              |
                              v
BattleGameMode -> observer snapshot + pending request
                              |
                              v
                 FBattlePresentationAdapter
                              |
                              v
                    FBattleHUDDisplayState
                              |
                 atomic validation/application
                              v
       collapsed HUD generation -> visible ready HUD
```

### Key Interfaces

The names below define responsibilities; exact member spelling may be refined during implementation without changing ownership.

```cpp
struct FBattleRuntimeBundle
{
    TUniquePtr<FBattleEngine> Engine;
    FTrainerId LocalTrainerId;
    TSharedPtr<const IBattleDisplayNameResolver> DisplayNames;
};

class IBattleRuntimeSource
{
public:
    virtual ~IBattleRuntimeSource() = default;
    virtual bool TryCreateInitialBattle(
        FBattleRuntimeBundle& OutBundle,
        FString& OutError) const = 0;
};

class IBattleDisplayNameResolver
{
public:
    virtual ~IBattleDisplayNameResolver() = default;
    virtual bool TryResolveSpeciesName(
        FSpeciesFormId SpeciesFormId,
        FText& OutDisplayName) const = 0;
};
```

The HUD exposes one atomic state-application operation and a read-only presentation generation. GameMode never initializes health panels and command state through unrelated calls.

## Alternatives Considered

### Alternative 1: Keep the hardcoded setup but move it from UI to Battle runtime

- **Description**: Relocate the current constants and builders without changing how data is authored.
- **Pros**: Smallest code change; preserves the current matchup immediately.
- **Cons**: Continues to duplicate canonical content, cannot use the existing DataTable adapter, and will drift when production content arrives.
- **Rejection Reason**: It fixes directory ownership but not the data-driven or dependency-injection failures.

### Alternative 2: Defer all production data until the later full content package

- **Description**: Remove the fixed runtime Battle or leave the HUD unavailable until the planned complete data pipeline arrives.
- **Pros**: Avoids bringing any content work forward.
- **Cons**: Breaks the approved playable FoundationMap path and leaves the current presentation lifecycle untestable in production.
- **Rejection Reason**: The user selected a narrow production data package so the current approved matchup remains playable.

### Alternative 3: Read JSON directly at runtime

- **Description**: Parse files from `SourceData` or a packaged loose-data directory when Battle begins.
- **Pros**: Avoids creating Unreal assets and makes text edits immediately visible.
- **Cons**: Creates platform, cooking, localization, path, and validation risks; bypasses Unreal asset references; and makes missing loose files a shipping failure.
- **Rejection Reason**: JSON is an authoring format, not the shipped runtime dependency.

### Alternative 4: Reveal an empty HUD, then fill each panel independently

- **Description**: Keep the current viewport timing and initialize names, HP, and commands as data arrives.
- **Pros**: Fewer lifecycle changes.
- **Cons**: Exposes incomplete or misleading state, allows partial mutation, and contradicts the Battle HUD fail-closed contract.
- **Rejection Reason**: Required initial presentation must validate atomically before any root HUD becomes visible.

## Consequences

### Positive

- Gameplay data leaves the UI module and becomes inspectable, reproducible, and replaceable without C++ edits.
- The approved prototype uses the same catalog-validation path intended for broader content.
- A HUD can no longer appear with missing names, HP, or command state.
- Controller and HUD replacement becomes deterministic.
- Tests exercise production construction rather than only injected class-default objects.
- Interface injection keeps setup sourcing and display-name lookup independently testable.

### Negative

- The project gains a small import pipeline and nine initial data assets.
- Startup performs a synchronous load of the fixed initial dataset. This is acceptable for the current tiny FoundationMap scenario but must be revisited before large catalogs or streamed encounters.
- A packaged-build check is required in addition to PIE automation.
- Supporting non-zero PP Ups remains deferred.

### Risks

- **Cooked dependency omission**: Soft references can work in Editor while assets are absent from a package. Mitigation: reflected soft references, explicit packaging inclusion for Battle data, HUD, and input assets, asset audit, and packaged Windows smoke testing.
- **JSON precision loss**: Numeric JSON values cannot safely represent every `uint64`. Mitigation: author them as decimal strings and reject invalid parsing.
- **UObject lifetime leakage**: Retaining DataTable row pointers in a plain provider could produce stale references. Mitigation: copy rows and localized text into owned runtime values immediately.
- **Repeated UMG construction**: Duplicate native bindings could double-deliver events. Mitigation: make bindings idempotent and test repeated construction/re-addition.
- **Partial HUD mutation**: A late validation error could leave mismatched panels. Mitigation: validate the complete display state and structural bindings before applying any field.
- **Late GameMode attachment**: The controller may signal readiness before GameMode binds. Mitigation: expose the current generation and refresh immediately on attachment.
- **Multiplayer misuse**: The local-player ownership assumption is not valid for replicated battles. Mitigation: record this as a single-player composition decision and require a separate multiplayer ADR before reuse.

## GDD Requirements Addressed

| GDD System | Requirement | How This ADR Addresses It |
|------------|-------------|--------------------------|
| `design/gdd/game-concept.md` | Pillar 4 requires the small initial experience to remain reusable rather than becoming a throwaway special case. | The fixed matchup uses generic reflected data, existing validators, derived stats, and an injected source instead of species-specific UI logic. |
| `design/ux/battle-hud.md` | Observer-safe Battle data and a display-name resolver feed `FBattlePresentationAdapter`; the HUD owns no Battle state. | The runtime bundle supplies a copied name resolver, and the adapter constructs one presentation-only state from observer-safe data. |
| `design/ux/battle-hud.md` | Invalid required initial data keeps the entire HUD hidden; later errors freeze the last valid presentation and stop command input. | Atomic validation gates root visibility, while later failures preserve visuals and disable submission. |
| `design/ux/battle-hud.md` | Health panels require authoritative active-Pokemon identity, display name, and HP; command selection requires a complete pending request and typed unavailable reasons. | One HUD display state contains both health projections and complete command availability derived from the same validated runtime observation. |

## Performance Implications

- **CPU**: One small synchronous startup conversion and validation pass; request refreshes remain event-driven with generation-aware deduplication.
- **Memory**: The initial catalog, setup, and localized names are copied into small owned runtime structures. DataTable row pointers are not retained.
- **Load Time**: A small fixed DataTable graph is synchronously loaded before the HUD appears. The 100 ms post-validation HUD appearance target remains in force.
- **Network**: None. This ADR covers local single-player presentation only.

## Migration Plan

1. Add reflected runtime and display-name rows plus the runtime-source and resolver interfaces.
2. Add initial JSON files and an Unreal Editor import script.
3. Build the runtime module so the row structures are available to the importer.
4. Import, save, and validate the nine DataTable assets; include their directory in packaging.
5. Implement the production DataTable runtime source and prove it reproduces the approved catalog, setup, stats, identities, policies, and seed.
6. Change GameMode to consume the interface and retain the immutable display-name resolver.
7. Add the atomic HUD display state, collapsed readiness handshake, and presentation generation.
8. Replace request-only caching with request-plus-generation caching.
9. Add fail-closed, mapping, replacement, changed-request, soft-load, widget lifecycle, FoundationMap PIE, and cooked-asset tests.
10. Remove `Private/UI/BattleRuntimeSetupSource.*` only after equivalence tests pass.
11. Run the focused automation reports and a packaged Windows smoke test; retain manual actual-size visual acceptance as a separate gate.

## Validation Criteria

- The initial data imports without diagnostics and builds a valid `FBattleDefinitionCatalog` and `FBattleSetup`.
- Derived Charizard and Venusaur stats, HP, moves, policies, identities, and deterministic seed match the approved matchup.
- Missing table, invalid row, unresolved name, invalid HP, missing widget binding, failed viewport attachment, and failed command-state application all keep an initial HUD collapsed and command input disabled.
- A later invalid update preserves the last valid visible state and disables command input.
- The same request is deduplicated only for the same HUD generation.
- Replacing the controller or HUD redelivers the current request.
- Changed requests always redeliver, and failed application is never cached as presented.
- All ten approved unavailable-reason mappings and unsupported kind/reason pairs are asserted.
- Automated FoundationMap PIE loads the production soft classes, creates and attaches the real HUD, runs native binding lifecycle, presents authoritative names/HP/commands, and reveals the root only after success without a manual readiness broadcast.
- An asset dependency audit or packaged Windows run proves the initial Battle DataTables, HUD Blueprint, and input assets are cooked and loadable.
- Manual PIE at 1920x1080 and 1280x720 remains required before visual acceptance.

## Related Decisions

- `design/gdd/game-concept.md`
- `design/ux/battle-hud.md`
- `design/quick-specs/reusable-stats-based-damage-addition-2026-08-20.md`
- `production/roadmap/battle-system-implementation-roadmap.md`
