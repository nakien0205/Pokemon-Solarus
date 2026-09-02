# C10A Canonical Proof Content — Historical Draft and Gate Register

Status: **C10A SOURCE JSON COMPLETE; C10B IMPORT/CATALOG ACCEPTED**

Current planning authority: `plan/battle_mechanics/11-canonical-proof-content.md`.
This document preserves the detailed R0–R7/C10A implementation and evidence
history; it no longer owns the live C10B boundary.

Date: 2026-08-30

Primary verdict: **C10A AND C10B COMPLETE; C11A IS NEXT**

R0 source/scope decision gate: **PASS — accepted 2026-08-28**

Remediation status: **R1 through R7, C10A, and C10B COMPLETE; C11A is next**

Direct C10A row authoring is complete. R1 through
R6 provide reusable typed implementations for B02 through B12. A separate
read-only R7 session accepted the live source and evidence, and the pinned-source
extraction manifest now exists. The completed sequence was:

1. preserve the completed R0 source/scope decisions in this document and the
   dated B00B amendment;
2. add only the missing reusable mechanics in their owning packages, in
   separately approved sessions;
3. preserve the completed independent blocker gate and source manifest;
4. obtain exact approval for the six-file C10A data-only write;
5. author and statically validate the complete C10A source-JSON slice; and
6. perform and accept C10B import and integration in its own session.

The original R0 session authorized documentation changes only in this document
and the accepted B00B modern-rules snapshot. Each later remediation lane had its
own approval. R7 and the source manifest prove readiness; they do not expand the
six-file C10A boundary or authorize Unreal assets or Git changes.

## 1. What this audit can and cannot prove

This audit is exhaustive for:

- every row and mechanic explicitly named by C10A;
- all 62 selected C10A moves;
- all selected species, natures, types, Abilities, items, and conditions;
- the live schema-to-adapter-to-runtime path needed by those rows; and
- future-extension limits visible in the current definitions and runtime.

It is not an exhaustive list of every mechanic needed by an entire Pokémon
game. There is no approved full Pokédex, move catalog, item catalog, encounter
catalog, progression design, learnset design, or transformation-mechanic
specification from which such a claim could be proved.

C10 is deliberately a proof catalog, not the complete game catalog. Its
acceptance section explicitly forbids adding a full Pokédex, move catalog, or
item catalog. The target slice is 8 species/forms, 25 natures, 62 moves,
8 Abilities, 14 items, 40 conditions, 18 types, and 324 type pairs.

The scalable promise must therefore be precise:

- a later row that uses mechanics already represented by typed schema and
  runtime capabilities should be data-only;
- a later row that introduces a new mechanic must first add one reusable typed
  capability in the package that owns that mechanic; and
- no generic system may branch on a species, move, Ability, item, or condition
  ID.

It would not be credible to promise that all future Pokémon content will be
data-only with the current schema.

## 2. Live authority and checkout

Authorities were applied in this order:

1. CLAUDE.md as the repository's main rule;
2. docs/code-file-organization.md for the mandatory file map and validation
   rules;
3. plan/battle_mechanics/11-canonical-proof-content.md as the live C10
   implementation contract;
4. production/session-state/active.md for current package and worktree state;
5. the accepted B00B modern-rules snapshot and its source precedence;
6. the completed C02 through C09 package contracts;
7. live headers, source, tests, JSON, importer, and assets; and
8. pinned external sources only where the higher authorities do not supply a
   value.

Original R0 checkout facts (historical baseline):

| Fact | Verified value |
|---|---|
| Branch | main |
| HEAD | d37582e8beead6943c2cea41a93e8f6f8ab15bfc |
| C10 entry gate | b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0 is an ancestor of HEAD |
| C10 state | Not started; C10A is next |
| Required roadmap order | C10A, then C10B, then C11A, then C11B |
| Frozen feature | Cry for Help and wild reinforcement remain frozen |

Current remediation state:

| Lane | State | Durable evidence |
|---|---|---|
| R1 target vocabulary | COMPLETE | Published in `06d884e`; B02 and B03 resolved |
| R2 action-scoped redirection | COMPLETE | Published in `cf8b3e6`; B04 resolved |
| R3 ally action power modifier | COMPLETE | Fresh forced-Unity build, 11 focused successes, and 13 clean affected filters on 2026-08-29; B05 resolved |
| R4A hit qualifiers | COMPLETE | Final code and test-evidence reviews PASS; forced-Unity build, 7 focused successes, and 11 clean affected filters on 2026-08-29; B06, B07, and B08 resolved |
| R4B weather move rules | COMPLETE | Final code review APPROVED and test-evidence review ADEQUATE/COMPLETE; forced-Unity build, 8 focused successes, and 11 clean affected filters on 2026-08-29; B09 and B10 resolved |
| R5 held-item move intents | COMPLETE | Final code review APPROVED and test-evidence review ADEQUATE/COMPLETE; forced-Unity build, 12 focused successes, and 12 clean affected filters on 2026-08-30; B11 resolved |
| R6 optional condition removal | COMPLETE | Final code review APPROVED and test-evidence review ADEQUATE/COMPLETE; forced-Unity build, 7 focused successes, and 7 clean affected filters on 2026-08-30; B12 resolved |
| R7 independent blocker gate | COMPLETE | Independent code review APPROVED and test-evidence review ADEQUATE/COMPLETE; 22 clean serial reports, 390/390 full-Battle successes, and no source-hash mismatch on 2026-08-30 |
| C10A source extraction | COMPLETE | Reproducible pinned-source recipe and manifest passed exact counts and source hashes on 2026-08-30 |
| C10A source rows | COMPLETE | Six approved JSON files passed bounded static validation on 2026-08-30 |
| C10B import/catalog | COMPLETE | Seven-table import, catalog/runtime equivalence, forced-Unity build, 21 Python tests, and 187/187 serial Automation successes accepted on 2026-08-31 |
| Next lane | C11A | Requires a fresh implementation-approval task |

C10A source-row authoring and C10B import/catalog acceptance are complete.
Required continuation: `C11A -> C11B`.

Pre-existing dirty inventory that every later session must preserve:

- No follow-up architecture records are part of this historical C10A draft.

Both paths are untracked. No tracked dirty path was present before this
document was created.

## 3. Pre-C10A content baseline and completed target

| Family | Pre-C10A rows | Completed C10A target | Change later required |
|---|---:|---:|---:|
| Species/forms | 2 | 8 | +6 |
| Natures | 1 | 25 | +24 |
| Moves | 2 | 62 | +60 |
| Abilities | 2 | 8 | +6 |
| Items | 0 | 14 | +14 |
| Conditions | 1 | 40 | +39 |
| Types | 18 | 18 | 0 |
| Type pairs | 324 | 324 | 0 |

Pre-C10A rows were Charizard, Venusaur, Hardy, Flamethrower, Vine Whip, Blaze,
Overgrow, and Burn. items.json is an empty array. The type chart is already
complete and is validate-only for C10A.

Protected source-JSON baseline:

| File | SHA-256 |
|---|---|
| species_forms.json | 1aac5a696ea2cf4b31c0ef25b5322733a79ab77e8966aafdd8f6cc7bb3b14e88 |
| natures.json | c868eaab61f5bbf88566479a1e2f91303f017f01bd804e38e620b61d687e8f1e |
| moves.json | 28da0197bb08877df298971754b245abee5e5b91b64d96ce2a57d7ed476d0cf2 |
| abilities.json | 3dc99ebc26a5c8ed6a90094999aef25b5249ffefd85ff5e9cb0996f47ff6c435 |
| items.json | 37517e5f3dc66819f61f5a7bb8ace1921282415f10551d2defa5c3eb0985b570 |
| conditions.json | 404feb0de118dcf1e0987df863f4ba8c37058761fe167990b17aa95a425471ba |
| type_chart.json | 896b7c674cb41a447251b9314bbb21773bdf4f2396592c9129f41cd8c6b67592 |

These hashes are planning evidence, not acceptance evidence. A future
implementation session must record fresh pre-write hashes.

## 4. Finding classifications

Every non-ready finding uses one of these labels:

- **CONFLICT** — two applicable authorities imply different behavior.
- **ADDITIVE GAP** — the existing design is valid but lacks one reusable typed
  capability.
- **SEQUENCING OVERLAP** — a required capability belongs to an earlier package
  or crosses an already-frozen checkpoint.
- **UNKNOWN** — evidence is insufficient to choose a value or exact rule.

## 5. Contract-to-source matrix

| Contract | Live evidence | Classification | Required disposition |
|---|---|---|---|
| C10A must provide the complete bounded row set | The baseline JSON contained only the rows in section 3; C10A now contains the complete target | RESOLVED | Preserve the complete rows for C10B import |
| No move-ID branches | BattleDefinitions.h provides typed target, flag, and effect vocabularies | — | Extend append-only typed vocabularies; never inspect MoveId for behavior |
| Swift must hit the opposing spread only | `FixedOpponentSpreadSet` is implemented and validated | RESOLVED | Author the row only in C10A |
| Fly must target any other active battler | `SelectedOtherBattler` is implemented and validated | RESOLVED | Author the row only in C10A |
| Follow Me must feed redirection | Typed private action-scoped redirection is implemented and validated | RESOLVED | Author the row only in C10A; no condition row |
| Helping Hand must modify an ally's action | Typed private ally action power modifiers are implemented and validated | RESOLVED | Author the row only in C10A; no condition row |
| Thunder Wave must respect Electric move immunity | R4A implemented and validated the authored status-move type-immunity trait before accuracy | RESOLVED | Author the row only in C10A |
| Powder moves must respect powder immunity | R4A implemented and validated the authored Powder trait and Grass-target gate before accuracy | RESOLVED | Author the rows only in C10A |
| Toxic's Poison-user hit rule must be exact | R4A implemented and validated the typed Poison-user reachability and accuracy bypass while preserving later gates | RESOLVED | Author the row only in C10A; never inspect Toxic's ID |
| Solar Beam and Thunder weather behavior must be exact | R4B implemented and validated reusable authored weather charge, power, and accuracy rules | RESOLVED | Author the rows only in C10A; never inspect either move ID |
| Item-changing moves must reach the held-item ledger | R5 implemented and validated generic remove-current, exchange-current, transfer-current, and restore-last-consumed intents through the existing ledger, staged mirrors, hooks, reveal tracker, and public events | RESOLVED | Author the rows only in C10A; never inspect move or item IDs |
| Removal moves must tolerate absent conditions | R6 implemented and validated typed `OptionalIfAbsent` removal while preserving legacy absence failure and effect ordering | RESOLVED | Author the rows only in C10A; never inspect a move or condition ID for optional behavior |
| C10B must validate before asset mutation | C10B added pure all-source preflight, an exact seven-family allowlist, and seven transient no-package conversions before any production-table load | RESOLVED | Preserve this import boundary; type chart and runtime scenario remain validate-only |
| Later Ability/item/condition rows should scale | Their definitions contain only identity or family; approved behavior is currently mapped through exact canonical IDs | ADDITIVE GAP | Accept that new mechanics need owning-package work, or separately approve typed behavior payloads later |

## 6. Complete 62-move expressibility matrix

Status meanings:

- **READY** — current typed capabilities can express the selected C10 behavior;
  C10B subsequently imported and proved every selected row.
- **BLOCKED** — a confirmed generic capability is absent.

| # | Move | Generic encoding or missing seam | Status | Finding |
|---:|---|---|---|---|
| 1 | Flamethrower | Selected-opponent Damage, then 10% Burn | READY | — |
| 2 | Vine Whip | Selected-opponent Damage | READY | — |
| 3 | Quick Attack | Selected-opponent Damage with authored priority | READY | — |
| 4 | Swift | Always-hit Damage to a foe-only spread | READY | — |
| 5 | Earthquake | FixedSpreadSet Damage plus ReducedByGrassyTerrain | READY | — |
| 6 | Follow Me | Self action that registers turn-scoped redirection | READY | — |
| 7 | Helping Hand | Selected-ally action power modifier | READY | — |
| 8 | Swords Dance | User Attack stage +2 | READY | — |
| 9 | Thunder Wave | Paralysis plus status-move type immunity | READY | — |
| 10 | Will-O-Wisp | Selected-opponent Burn application | READY | — |
| 11 | Sleep Powder | Sleep application plus Powder trait | READY | — |
| 12 | Ice Beam | Damage, then 10% Freeze | READY | — |
| 13 | Poison Powder | Poison application plus Powder trait | READY | — |
| 14 | Toxic | Toxic application plus Poison-user semi-invulnerability and accuracy bypass | READY | — |
| 15 | Confuse Ray | Confusion application | READY | — |
| 16 | Bite | Contact Damage, then 30% Flinch | READY | — |
| 17 | Protect | Priority self Protect effect | READY | — |
| 18 | Leech Seed | Selected-opponent Leech Seed volatile | READY | — |
| 19 | Wrap | Damage, then partial-trap volatile | READY | — |
| 20 | Mean Look | Selected-opponent switch-prevention trap | READY | — |
| 21 | Taunt | Selected-opponent Taunt volatile | READY | — |
| 22 | Encore | Selected-opponent Encore volatile | READY | — |
| 23 | Disable | Selected-opponent Disable volatile | READY | — |
| 24 | Substitute | User Substitute volatile and HP cost | READY | — |
| 25 | Solar Beam | Charge then Damage; Sun skips charge and Rain/Sandstorm/Snow halve power | READY | — |
| 26 | Hyper Beam | Damage, then user Recharge | READY | — |
| 27 | Fly | Charge, semi-invulnerability, then selected-other Damage | READY | — |
| 28 | Thunder | Damage, Fly reach, 30% Paralysis; Rain always-hit and Sun 50% accuracy | READY | — |
| 29 | Bullet Seed | Ranged MultiHit plus Damage | READY | — |
| 30 | Giga Drain | Damage plus one-half actual-damage Drain | READY | — |
| 31 | Double-Edge | Damage plus one-third actual-damage Recoil using Solarus half-up rule | READY | — |
| 32 | Recover | User Heal for one-half base Max HP | READY | — |
| 33 | U-turn | Damage, then deferred user pivot Switch | READY | — |
| 34 | Roar | Deferred forced target Switch with authored priority | READY | — |
| 35 | Knock Off | Damage, typed takeability power rule, then remove current target item | READY | — |
| 36 | Trick | Exchange the users' current held items, including one-empty cases | READY | — |
| 37 | Thief | Damage, then conditionally transfer target item to an empty user | READY | — |
| 38 | Recycle | Restore the user's most recently consumed eligible item instance | READY | — |
| 39 | Rapid Spin | Damage, optional user/user-side removals, then Speed +1 | READY | — |
| 40 | Defog | Evasion -1 plus optional target-side, both-side, and field removals | READY | — |
| 41 | Brick Break | Optional target-side screen removals before Damage | READY | — |
| 42 | Sunny Day | Set Sun field condition | READY | — |
| 43 | Rain Dance | Set Rain field condition | READY | — |
| 44 | Sandstorm | Set Sandstorm field condition | READY | — |
| 45 | Snowscape | Set Snow field condition | READY | — |
| 46 | Electric Terrain | Set Electric Terrain field condition | READY | — |
| 47 | Grassy Terrain | Set Grassy Terrain field condition | READY | — |
| 48 | Misty Terrain | Set Misty Terrain field condition | READY | — |
| 49 | Psychic Terrain | Set Psychic Terrain field condition | READY | — |
| 50 | Spikes | Set target-side layered hazard | READY | — |
| 51 | Toxic Spikes | Set target-side layered hazard | READY | — |
| 52 | Stealth Rock | Set target-side hazard | READY | — |
| 53 | Sticky Web | Set target-side hazard | READY | — |
| 54 | Reflect | Set user-side screen | READY | — |
| 55 | Light Screen | Set user-side screen | READY | — |
| 56 | Aurora Veil | Set user-side screen with Snow activation rule | READY | — |
| 57 | Trick Room | Set/toggle field room | READY | — |
| 58 | Magic Room | Set/toggle field room | READY | — |
| 59 | Wonder Room | Set/toggle field room | READY | — |
| 60 | Tailwind | Set user-side condition | READY | — |
| 61 | Safeguard | Set user-side condition | READY | — |
| 62 | Mist | Set user-side condition | READY | — |

Totals: 62 READY and 0 BLOCKED, with no unresolved decision rows.

The READY label records the C10A expressibility decision. Accepted C10B
evidence subsequently proved the rows through imported Data Tables.

## 7. Non-move family matrix

| Family | C10A expressibility | Potential blocker |
|---|---|---|
| Species/forms | Current schema holds stable form ID, two types, six base stats, catch rate, and Ability choices | No source blocker after R0; the pinned PokeAPI supplement supplies only the approved catch rates |
| Natures | Current modifier supports neutral and boost/reduction forms | No schema blocker found |
| Types/type chart | Complete 18 types and 324 pairs already exist | Validate-only; any byte change is out of scope for C10A |
| Abilities | All eight selected IDs already have live canonical behavior | New future Ability IDs are not behavior-data-only |
| Held items | All nine selected IDs have live canonical behavior and ledger identity; R5's four typed move operations bridge into that ledger | No remaining held-item move blocker; C10A authored all selected rows |
| Bag/capture items | All five selected IDs have live rule owners | New future item kinds require owning-package extensions |
| Conditions | All 40 selected IDs correspond to the existing major, volatile, field, hazard, screen, room, and side-condition families | New future condition behavior is not created by adding an identity row |
| WildFlee | Engine action and encounter policy already own it | C10 uses a synthetic configured fixture, not a species default or normal move row |
| Struggle | Engine-supplied immutable fallback already owns it | It must not be added to moves.json |
| Cry for Help/reinforcement | Frozen by explicit Solarus decision | No row, code, or test may be added without a new user decision |

## 8. Blocker register

### B01 — Catch-rate source decision resolved

- R0 status: **RESOLVED**.
- Evidence: the pinned Showdown Pokédex data provides typing, base stats, and
  Abilities but not catch rates.
- Approved supplement: PokeAPI commit
  7af36d9f3424366ffc46e90d94c8bc120df39cd0, file
  data/v2/csv/pokemon_species.csv, raw-byte SHA-256
  9878f19c0637095cdd9a4134b4aac8fb2b64776d3bdc599aa68f15c3a011b87c.
- Approved values are Charizard 45, Venusaur 45, Gyarados 45, Rotom 45,
  Pelipper 45, Espathra 60, Clefable 25, and Excadrill 60.
- Scope rule: use this PokeAPI source for `capture_rate` only. Showdown remains
  the source for the other selected species facts.

### B02 — Foe-only fixed spread target resolved

- Classification: **RESOLVED by R1**
- Gate: Swift is expressible; C10A authored its row.
- Owner: C02B definition vocabulary plus C04B target resolution.
- Required capability: append a target class such as FixedOpponentSpreadSet.
- Stable-contract rule: do not renumber or broaden FixedSpreadSet; Earthquake
  depends on its existing ally-inclusive meaning.
- Required proof: Singles/Doubles target order, no ally inclusion, fainted and
  unavailable slots excluded, no RNG, exact target events, replay identity,
  and no change to Earthquake-style spread behavior.

### B03 — Selected-other-battler target resolved

- Classification: **RESOLVED by R1**
- Gate: Fly is expressible; C10A authored its row.
- Owner: C02B/C04B.
- Required capability: append a target class such as SelectedOtherBattler.
- Stable-contract rule: do not change AnySelectedBattler, which currently and
  deliberately includes the user.
- Required proof: self is never legal, ally and opponents are legal where
  format permits, explicit target is required, no enum ordinal changes, and
  existing selected-target replay facts remain stable.

### B04 — Typed redirection producer resolved

- Classification: **RESOLVED by R2**
- Gate: Follow Me is expressible; C10A authored its row.
- Owner: C04B target resolution with C05/C07 action-scoped state and trigger
  integration.
- Evidence: FBattleTargetResolutionSpec already accepts ordered
  RedirectionProposals; production code never populates the array.
- Required capability: a typed turn-scoped redirection registration created by
  a generic move effect and projected into the existing proposal list.
- Required proof: priority/order, multiple redirectors, legal-target filtering,
  expiry at the exact turn boundary, capture/faint/switch cleanup, no extra RNG,
  exact event order, and atomic rollback.
- Do not add an unapproved Condition.FollowMe row merely to route behavior.

### B05 — Action-scoped ally power modifier resolved

- Classification: **RESOLVED by R3**
- Gate: Helping Hand is expressible; C10A authored its row.
- Owner: C05 effect execution and damage input, with turn/action lifecycle
  state.
- Required capability: an appended typed effect that registers a rational
  modifier against one eligible ally action and expires deterministically.
- Required proof: target eligibility, already-acted target, faint/switch/cancel
  cleanup, one-time application, stacking policy from the pinned rule source,
  no rounding before the approved damage checkpoint, event order, and replay.
- Do not add Condition.HelpingHand or inspect Move.HelpingHand in generic code.

### B06 — Status moves cannot opt into move-type immunity

- Implementation status: **RESOLVED IN CODE by R4A**.
- Original classification: **ADDITIVE GAP**.
- Current gate: closed; C10A authored the row.
- Owner: C05 hit pipeline.
- Original evidence: before R4A, damaging moves obtained pre-accuracy type
  no-effect resolution from damage input while status moves had no authored
  opt-in.
- Implemented capability: an authored typed trait checks the move's type-chart
  immunity before Ability/item immunity and before accuracy.
- R4A proof: Thunder Wave versus Ground consumes no accuracy RNG and
  applies no Paralysis; Confuse Ray and other status moves that ignore ordinary
  type immunity remain unchanged.

### B07 — No Powder trait or Powder immunity gate

- Implementation status: **RESOLVED IN CODE by R4A**.
- Original classification: **ADDITIVE GAP**.
- Current gate: closed; C10A authored the rows.
- Owner: C05 hit/immunity rules, with existing Ability/item immunity hooks
  available for later extensions.
- Implemented capability: an authored Powder move trait; Grass targets block it
  at the move-immunity checkpoint.
- R4A proof: Grass immunity occurs before accuracy and consumes no
  accuracy/status RNG; non-Grass targets retain normal accuracy; future
  Ability/item powder immunity can attach without move-ID branches.

### B08 — Poison-user Toxic needs a typed conditional hit rule

- R0 status: **RESOLVED**.
- Implementation status: **RESOLVED IN CODE by R4A**.
- Classification after R0: **ADDITIVE GAP**, now closed.
- Current gate: closed; C10A authored the final Toxic row.
- Evidence: pinned Showdown bypasses both its invulnerability event and its
  accuracy roll for a Generation 8+ Poison-type Toxic user.
- Approved behavior: a Poison-type user bypasses semi-invulnerability and
  accuracy without consuming accuracy RNG. Protect and every later applicable
  hit, immunity, status, and effect gate remains in order. A non-Poison user
  keeps ordinary `90` accuracy and reachability.
- Implemented capability: a typed conditional hit rule based on user typing,
  never a Toxic ID check.
- R4A proof: Poison and non-Poison users across ordinary and
  semi-invulnerable targets, no accuracy draw on the approved bypass,
  ordinary RNG for the non-Poison route, Protect still blocking, later gates
  unchanged, exact events, replay, and rollback.

### B09 — Solar Beam needs typed weather charge and power rules

- R0 status: **RESOLVED** by a narrow B00B amendment.
- Implementation status: **RESOLVED IN CODE by R4B**.
- Classification after R0: **ADDITIVE GAP**, now closed.
- Current gate: closed; C10A authored the Solar Beam row.
- Approved behavior: Sun skips the charging turn. Rain, Sandstorm, and Snow
  halve move power at the damage execution checkpoint. Other approved weather
  leaves power unchanged. Without Sun, Solar Beam remains a two-turn charge
  move.
- Scope: primal weather and Hail remain excluded. R4B uses authored
  weather/charge/power rules that can support later related moves and contains
  no Solar Beam ID check.
- R4B proof: charge entry and skip, PP and target-lock behavior, weather
  changes between charge and execution, exact half-power arithmetic, supported
  and unsupported weather, event/RNG order, replay, cleanup, and rollback.

### B10 — Thunder needs a typed weather-accuracy rule

- R0 status: **RESOLVED** by a narrow B00B amendment.
- Implementation status: **RESOLVED IN CODE by R4B**.
- Classification after R0: **ADDITIVE GAP**, now closed.
- Current gate: closed; C10A authored the Thunder row.
- Approved behavior: base accuracy is `70`; Rain changes it to literal
  always-hit with no accuracy RNG; Sun changes it to `50`; other approved
  weather leaves it at `70`. Existing Fly reach and `30%` Paralysis remain.
- Scope: primal weather remains excluded. R4B uses an authored
  weather-accuracy rule and contains no Thunder ID check.
- R4B proof: each supported weather, Rain's zero-draw route, Sun and
  ordinary accuracy draws, Fly reachability before accuracy, secondary RNG
  only after a hit, exact events/replay, and rollback.

### B11 — Authored item moves reach the existing ledger — RESOLVED

- Classification: **RESOLVED by R5 on 2026-08-30**.
- Gate: Knock Off, Trick, Thief, and Recycle are expressible; C10A authored
  their source rows.
- Owner: C05 descriptor/executor intent plus C08 held-item ownership and hook
  lifecycle. R5 added no second ledger, state owner, RNG owner, commit seam, or
  publication path.
- Implemented capability: the appended authored operation values
  `RemoveCurrent`, `ExchangeCurrent`, `TransferCurrent`, and
  `RestoreLastConsumed` create typed intents. The staged executor resolves live
  instance IDs and applies the existing Remove, Restore, Swap, and
  TemporarilySteal ledger operations together with battler mirrors, hook
  ownership, reveal state, Choice-lock cleanup, and public events.
- Knock Off uses the item definition's data-driven `bCanBeTakenByMove` policy
  and a Q12 `6144` power modifier. Suppression does not make a takeable item
  unremovable. Recycle selects the currently consumed item with the greatest
  matching consumption-fact ordinal for the acting Trainer/battler.
- Persistent items already consumed before setup may carry no battle-consumer
  history and are deliberately excluded from Recycle lookup. Partial history,
  nonzero ordinals without a consumer, restored items without history, and
  battle-generated consumed items without history remain invalid.
- Proof covers Protect, immunity, miss, Substitute, faint, empty holders,
  suppression, takeability, hook/reveal/mirror synchronization, Choice locks,
  event order and schema-6 serialization, final ownership facts, transactional
  RNG, and late rollback boundaries.

### B12 — Canonical removal moves need optional absence semantics

- Classification: **RESOLVED ADDITIVE GAP — R6 COMPLETE 2026-08-30**
- Gate: resolved; Rapid Spin, Defog, and Brick Break are expressible and C10A
  authored their rows.
- Owner: C05 descriptor/executor with C07 condition cleanup.
- Existing reusable base: ordered RemoveCondition descriptors can target a
  battler, user side, target side, both sides, or field; primary descriptors
  ordered before Damage already run at the pre-damage checkpoint.
- Resolved rule: an absent condition still fails for legacy removals, but an
  authored `OptionalIfAbsent` removal succeeds silently while later effects
  continue. Present conditions still run their normal cleanup exactly once.
- Rapid Spin proof: only after a connected hit, including Substitute damage;
  remove the approved user volatiles and user-side hazards, then apply Speed
  +1.
- Defog proof: drop target Evasion and remove its exact approved target-side,
  both-side, and terrain sets without failure noise for absent entries.
- Brick Break proof: remove Reflect, Light Screen, and Aurora Veil before
  damage, through Substitute, but remain blocked by Protect. The existing
  BreaksProtection flag must not be reused; that flag breaks Protect itself.

### B13 — Historical importer boundary resolved by C10B

- Classification: **RESOLVED**
- Gate: completed and accepted in C10B on 2026-08-31.
- Historical evidence: import_initial_battle_data.py preloaded JSON
  syntax/name checks, then
  imports and saves all nine tables, including display names and runtime
  scenario. It does not perform complete schema/cross-reference/catalog
  validation before asset mutation.
- Historical required capability: a pure preflight path and explicit family
  allowlist. At the time, the draft expected only six changed Data Tables. Live
  inspection later proved that the runtime resolver also needs six missing
  species display names, so section 12 and the canonical C10 plan supersede
  that boundary with seven changed Data Tables. Type chart and runtime scenario
  remain validate-only.
- Required proof: any syntax, reflected-row, missing reference, wrong family,
  duplicate ID, bad range, or incomplete catalog error leaves all loaded
  assets and the already-frozen catalog unchanged.
- Implemented disposition: the importer now runs a pure nine-document
  validation and stages all seven mutable families in transient no-package Data
  Tables before loading a production table. Fake-service tests prove preflight,
  staging, live-fill rollback, save and post-save failures, and exact seven-file
  binary restoration. Accepted evidence is rooted at
  `Game/Saved/AutomationReports/C10B-ImportAndCatalog-20260831-090312`.

### B14 — Ability, item, and condition rows do not carry behavior payloads

- Classification: **ADDITIVE GAP**
- Gate: does not block the approved C10 proof slice; it blocks a broad
  rows-only promise for future catalogs.
- Evidence: FBattleAbilityDefinition contains only Id;
  FBattleItemDefinition contains Id and Kind; FBattleConditionDefinition
  contains Id and Kind. Runtime rule-kind selection recognizes the approved
  canonical IDs.
- Disposition: do not redesign all three families inside C10. When a later
  package admits a new behavior family, add a typed rule kind or hook payload
  in that owning package and migrate only the necessary rows. A universal
  scripting engine is not justified by this proof slice.

### B15 — The current move-tag vocabulary is only the approved slice

- Classification: **ADDITIVE GAP**
- Gate: does not block C10 because no selected C10 interaction consumes these
  tags.
- Visible later families include bite, bullet, sound, dance, wind, healing,
  reflection, snatch, copy/metronome restrictions, and similar canonical move
  traits.
- Disposition: add an append-only typed tag only when an approved mechanic
  consumes it. Bite being in C10 does not justify implementing Strong Jaw now.

### B16 — Full-game species and move schemas are intentionally incomplete

- Classification: **ADDITIVE GAP**
- Gate: does not block C10.
- Species/forms do not yet model learnsets, evolution, breeding, gender,
  weight-based move inputs, encounter tables, held-item tables, growth, or
  non-battle Pokédex data.
- Moves do not yet model every variable/fixed-damage family, dynamic
  type/power, OHKO rules, copying/calling, transformation, every multi-turn
  family, or every modern special rule.
- Disposition: those systems need approved packages and typed generic
  extensions. They must not be smuggled into C10A.

### B17 — Reproducible pinned-source extraction

- Classification: **RESOLVED FOR C10A SOURCE FACTS**
- Gate: the pre-implementation extraction gate is closed. C10A acceptance must
  still compare the authored rows against this independent manifest.
- Required source order: the explicit dated R0 decisions, other unchanged
  Solarus decisions, pinned Showdown commit
  34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d, the other B00B sources, and the
  approved PokeAPI supplement for `capture_rate` only.
- Evidence root:
  `Game/Saved/AutomationReports/C10A-SourceContent-20260830-170739`.
- The rerunnable `extract-c10a-source-manifest.mjs` recipe fetched eight raw
  pinned files, verified every previously frozen raw-byte hash, retained each
  selected source block, and generated exact manifests for 8 species/forms,
  25 natures, 62 moves, 8 Abilities, 14 items, and 40 conditions.
- The canonical source manifest SHA-256 is
  `721EE44EF5D0EF6B9522D4BA1A94546D7032D1F17E1CAE812D52C4EC8D986629`;
  the selected-source-block manifest SHA-256 is
  `261D9F4F42E97BE4DA8A4F076931D6405A185ED8F7DE8FDCA0005FE744FE2047`.
- External source facts and explicit Solarus decisions are separate. Showdown
  target and flag names are evidence rather than direct Solarus enum mappings;
  ordered Solarus effects remain governed by this contract and the accepted
  R0 through R6 rules. PokeAPI remains limited to `capture_rate`.
- C10A must compare final rows against the generated source facts and must not
  rely on a hand-copied table alone.

## 9. Reusable design rules for later add-ons

Every remediation lane must follow these rules:

1. Preserve stable IDs and append enum values; never renumber a live enum.
2. Put reusable semantics in typed targets, flags, effect operations, rule
   kinds, or hook payloads.
3. Keep one owner for state, RNG, commit, and event publication.
4. Follow validate, prepare, resolve/stage, commit, then publish once.
5. A failed later checkpoint must not partially change state, RNG, replay
   facts, events, action progress, item ownership, or condition lifecycle.
6. Preserve already committed earlier checkpoints such as PP and frozen
   targets where the existing contract requires that.
7. Reject unknown authored capability names during catalog construction.
8. Do not add branches comparing MoveId, SpeciesFormId, AbilityId, ItemId, or
   ConditionId inside a generic executor to implement a new row.
9. Do not introduce a universal battle scripting language for C10. Add the
   smallest typed seam proved by at least one current row and designed for the
   visible related family.
10. A later row using an existing seam is data-only; a genuinely new mechanic
    returns to its owning package.

## 10. Required remediation sequence

Each lane below needs a fresh Question, Options, Decision, Draft, Approval
cycle. No lane inherits write approval from this document.

### R0 — Source and scope decisions

Status: **COMPLETE — documentation decisions accepted 2026-08-28**.

- Catch rates use the pinned PokeAPI CSV for `capture_rate` only.
- Poison-user Toxic bypasses semi-invulnerability and accuracy without an
  accuracy draw; non-Poison Toxic retains ordinary reachability and accuracy.
- B00B is narrowly amended for Showdown's supported Solar Beam and Thunder
  weather behavior.
- Raw committed bytes are the canonical cross-platform extraction hashes;
  the verified Windows CRLF hashes remain historical checkout evidence.

R0 authorized no implementation. R1 was later published in `06d884e`. At R0,
B08 through B10 remained blocked. R4A later resolved B08, and R4B later resolved
B09 and B10, under separate implementation approvals and focused evidence.

### R1 — Target vocabulary

Status: **PASS — decisions accepted 2026-08-28**.

Resolve B02 and B03 together because they append the same target taxonomy and
touch the same validators. Preserve every existing ordinal and meaning.

Maximum hand-authored write set:

- Game/Source/PokemonSolarus/Public/Battle/BattleSetupTypes.h
- Game/Source/PokemonSolarus/Public/Battle/BattleTargeting.h
- Game/Source/PokemonSolarus/Private/Battle/BattleTargeting.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleActionQueue.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEngineQueueBoundary.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEngineQueueBoundary.h
- Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleFaintOutcomeResolver.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleSetupTypesTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleActionQueueTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleTargetingTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleDefinitionCatalogTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleFaintOutcomeTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleAtomicMoveTargetTests.cpp

Any additional required path means the lane must stop and revise its draft
before editing it.

### R2 — Action-scoped redirection

Status: **COMPLETE — implementation and validation accepted 2026-08-28**.

Resolve B04 as a focused state/lifecycle family. A new focused file is required
instead of adding an independent responsibility to a large engine or executor
file.

Proposed maximum hand-authored write set:

- new Game/Source/PokemonSolarus/Private/Battle/BattleActionScopedMoveRules.h
- new Game/Source/PokemonSolarus/Private/Battle/BattleActionScopedMoveRules.cpp
- Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h
- Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorContext.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorConditions.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEngineMoveEffects.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEngineMoveTargets.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEngineEndTurn.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleState.h
- Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp
- Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp
- new Game/Source/PokemonSolarus/Private/Tests/BattleActionScopedMoveRuleTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleTargetingTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleAtomicMoveEffectTests.cpp

The lane draft must choose the exact private state owner and prove snapshot and
replay treatment before implementation.

### R3 — Action-scoped ally power modifier

Status: **COMPLETE — implementation and validation accepted 2026-08-29**.

Resolve B05 separately from redirection. It may reuse R2's typed
action-lifecycle contract, but it must not share a catch-all implementation
file.

The proposed list below is historical. It was superseded by the separately
approved exact 36-path R3 boundary, with focused
`BattleAllyActionPowerModifier` ownership and four focused test files.

Accepted evidence: forced-Unity log
`Game/Saved/Logs/R3-Final-ForcedUnity-20260829-102504.log` (SHA-256
`4DF6A8DA6670A161EEFDDEB77FFCC37BEF1D985CCD21DF9BA0FF9B1446B60144`)
and focused report
`Game/Saved/AutomationReports/R3-Focused-Final-20260829-102510/index.json`
(11 successes, no warnings or failures; SHA-256
`A1734190D6E47F54D98215782E0CA465DA849DE098F65D53953B9D619F8ADC37`).
The 13 serial affected filters contributed 200 overlapping executions and 177
unique full test paths, all successful. Both independent reviews passed.

Historical proposed maximum additional write set:

- Game/Source/PokemonSolarus/Private/Battle/BattleActionScopedMoveRules.h
- Game/Source/PokemonSolarus/Private/Battle/BattleActionScopedMoveRules.cpp
- Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h
- Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorContext.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorDamage.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEngineMoveEffects.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEngineEndTurn.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleState.h
- Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleActionScopedMoveRuleTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleAtomicMoveEffectTests.cpp

### R4A — Hit qualifiers

**COMPLETE — B06, B07, and B08 resolved.** R4A represents the approved behavior
as reusable authored traits and a typed user-condition hit rule, without any
move-ID, species-ID, Ability-ID, item-ID, condition-ID, or match-specific
production branch.

Actual hand-authored implementation set:

- new Game/Source/PokemonSolarus/Public/Battle/BattleMoveHitRules.h
- new Game/Source/PokemonSolarus/Private/Battle/BattleMoveHitRules.cpp
- Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h
- Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorContext.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorConditions.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorDamage.cpp
- new Game/Source/PokemonSolarus/Private/Tests/BattleMoveHitRuleTests.cpp

The public stateless `FBattleMoveHitRules` seam validates compatible authored
traits, resolves immutable catalog-authoritative user and target types, and
keeps hit qualification separate from state mutation. The executor preserves
the established order of reachability, Protect, TryHit, damaging type immunity,
move/Ability/item immunity, accuracy, protection breaking, and effects. The
outer checkpoint remains the sole owner of the final identity recheck,
transactional RNG commit or rollback, state application, and publication.

The focused identity is `PokemonSolarus.Battle.C05B.C10HitRules` and contains
exactly seven tests. Final post-review evidence is rooted at
`Game/Saved/AutomationReports/R4A-HitRules-PostReview-20260829-160909`; the
focused filter and eleven required affected filters passed 152/152 executions
with zero aggregate or per-test issues. The matching forced-Unity build log is
`Game/Saved/Logs/R4A-HitRules-PostReview-20260829-160909-Build.log`. Final
`code-review` and `test-evidence-review` verdicts were PASS with no remaining
validated finding.

### R4B — Weather move rules

**COMPLETE — B09 and B10 resolved.** The implementation uses the public
stateless `FBattleMoveWeatherRules` seam and three append-only authored flags.
It contains no Solar Beam, Thunder, raw condition-ID, or match-specific branch.
The existing outer transaction remains the only identity recheck, RNG commit,
state application, and event-publication owner.

Actual hand-authored production/test set:

- new Game/Source/PokemonSolarus/Public/Battle/BattleMoveWeatherRules.h
- new Game/Source/PokemonSolarus/Private/Battle/BattleMoveWeatherRules.cpp
- new Game/Source/PokemonSolarus/Private/Tests/BattleMoveWeatherRuleTests.cpp
- Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h
- Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorContext.h
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorConditions.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorDamage.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleFieldSideConditions.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleFieldSideConditionTests.cpp

The exact focused identities are:

1. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Contract.FlagsAdapterCatalogAndTriggerPhases`
2. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Charge.SunSkipOrdinaryChargePpTargetLockCleanup`
3. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Charge.ExecutionUsesCurrentWeather`
4. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Power.SupportedUnsupportedExactHalfAndPriority`
5. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Accuracy.RainSunOrdinaryDrawsAndSuppression`
6. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Order.FlyReachAccuracySecondaryAndEvents`
7. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Replay.DeterminismNoDuplicationAndCleanup`
8. `PokemonSolarus.Battle.C05B.C10WeatherMoveRules.Atomic.RollbackRngStateAndPublication`

Final evidence identity:
`Game/Saved/AutomationReports/R4B-WeatherMoveRules-PostReview-20260829-201311`.
Its forced-Unity build log passed all four required flags at SHA-256
`2A3BE861A4AC4BA0C07BFEA9176F7B21608BB10749D2E4D22D3AA1B179FFC595`.
The final report composition uses its post-review focused and C05B reports plus
the ten unchanged accepted reports from
`Game/Saved/AutomationReports/R4B-WeatherMoveRules-Acceptance-20260829-195514`:

| Filter | Success / warnings / failed / not run / in process | `index.json` SHA-256 |
|---|---:|---|
| `PokemonSolarus.Battle.C05B.C10WeatherMoveRules` | 8 / 0 / 0 / 0 / 0 | `DF110ECEA3AEA560297EC88B572FB30462B32EAC75C8D8C5F83A86ED325CE9E0` |
| `PokemonSolarus.Battle.C03A` | 6 / 0 / 0 / 0 / 0 | `004BBDA6DAB5B8CF2B806060E44AF3FE153F705B612F822BD870371FD0C72558` |
| `PokemonSolarus.Battle.C03B` | 6 / 0 / 0 / 0 / 0 | `069022E29F5895BA954406BA62367D4092FC24E794E6CB1C136AC6E12DB185D2` |
| `PokemonSolarus.Battle.C05A` | 8 / 0 / 0 / 0 / 0 | `70D80CDC5FF769E5D0A9F494A8146B370BEEC15B44E32544446D377EA76D090F` |
| `PokemonSolarus.Battle.C05B` | 35 / 0 / 0 / 0 / 0 | `5078CD86B9934D19986621384F5D217120E4BC5586E239B8D9C53FB96316B7AF` |
| `PokemonSolarus.Battle.C05C` | 7 / 0 / 0 / 0 / 0 | `E749AB00C7FD7799F4DDDABF6FF51BBF790C9EF5F29BA026E7C123CE751DA7F0` |
| `PokemonSolarus.Battle.C07B` | 9 / 0 / 0 / 0 / 0 | `583FE411FCB7405E7B286D613BEF58A16E47F3BCC6FF73A7515A52DFE3531190` |
| `PokemonSolarus.Battle.C07C` | 8 / 0 / 0 / 0 / 0 | `98F32B87867A0D787BCC70DC713681DFB2498E2B46CE606C4D97BF190066B5CB` |
| `PokemonSolarus.Battle.C07D` | 9 / 0 / 0 / 0 / 0 | `86E5CF00094D2198357597149EE899ECD704D11D7CDADF057666660A26AB9BC9` |
| `PokemonSolarus.Battle.C08B` | 20 / 0 / 0 / 0 / 0 | `CD0CFA74273975EA11ED1254F1FEC40DBCB367283F2CCD3DAEE5A188D32345E9` |
| `PokemonSolarus.Battle.C08C` | 27 / 0 / 0 / 0 / 0 | `78BE1C34FFFAC6618BF7D931BD0ACD3018D65D0DA06517C10C9535A4C3CFD445` |
| `PokemonSolarus.Battle.ADR0002.3E6` | 18 / 0 / 0 / 0 / 0 | `78C24B0299790E854E433BE2721DEC8C1A94377D1734F8B34201C472C3F36B47` |

The reports contain 161 overlapping executions and 153 unique full-test paths.
The counter manifest hash is
`1690F7F612585A631FE205993C0F62A15A1993C8449DE8DDBF6E14ECCF51A8C0`;
the exact path manifest hash is
`D9E3A0FB661D272AA13F382AAC334988B498D0737CC4728F7C9EC68974D7C1CF`.
Every aggregate and per-test issue counter is zero. Final `code-review` was
APPROVED and `test-evidence-review` was ADEQUATE/COMPLETE with no blocking or
advisory gap. This is not R7, and no C10A row was authored.

### R5 — Held-item move intents — COMPLETE

R5 resolved B11 by adapting authored move operations to the existing C08
ledger. It added no second ledger or item owner.

Implemented surface:

- `BattleHeldItemMoveEffects.h/.cpp` owns stateless authored-operation and
  takeability/power policy.
- `BattleEffectExecutorItemMoves.cpp` owns the typed intent bridge and atomic
  ledger/mirror/hook/reveal mutation workflow.
- `BattleAbilityItemContracts` records last consumer Trainer/battler and the
  successful consumption fact ordinal, supports deterministic latest-history
  lookup, and exposes direct public reveal synchronization.
- `ItemRestored = 55` and `ItemTransferred = 56` are append-only public event
  values. Replay schema remains `6`.
- Held-item intents resolve after generic effects and before forced switches and
  starting-Life-Orb recoil within the one staged execution plan. Life Orb
  recoil uses a per-move snapshot that proves its boost actually applied.

Final hand-authored production set:

- new `Game/Source/PokemonSolarus/Private/Battle/BattleHeldItemMoveEffects.h`
- new `Game/Source/PokemonSolarus/Private/Battle/BattleHeldItemMoveEffects.cpp`
- new `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorItemMoves.cpp`
- modified `Game/Source/PokemonSolarus/Public/Battle/BattleAbilityItemContracts.h`
- modified `Game/Source/PokemonSolarus/Public/Battle/BattleDataTableRows.h`
- modified `Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h`
- modified `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleAbilityItemContracts.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleAllyActionPowerModifier.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorAbilityItems.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorConditions.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorContext.h`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorDamage.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorState.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEngineEvents.h`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEngineMoveEffects.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp`
- modified `Game/Source/PokemonSolarus/Private/Battle/BattleMoveRedirection.cpp`

Final hand-authored test set:

- new `Game/Source/PokemonSolarus/Private/Tests/BattleHeldItemMoveEffectTests.cpp`
- modified `Game/Source/PokemonSolarus/Private/Tests/BattleAbilityItemContractTests.cpp`
- modified `Game/Source/PokemonSolarus/Private/Tests/BattleAllyActionPowerModifierTests.cpp`
- modified `Game/Source/PokemonSolarus/Private/Tests/BattleDataTableAdapterTests.cpp`
- modified `Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp`
- modified `Game/Source/PokemonSolarus/Private/Tests/BattleItemRuleTests.cpp`
- modified `Game/Source/PokemonSolarus/Private/Tests/BattleMoveRedirectionTests.cpp`

Organization decisions for changed files at or above 1,000 physical lines:

- `BattleEffectExecutor.cpp` remains the one generic descriptor/execution
  coordinator. R5 added validation and intent capture only; the independent
  item-mutation workflow was placed in `BattleEffectExecutorItemMoves.cpp`.
- `BattleEffectExecutorConditions.cpp` remains the condition/effect gate owner;
  R5 only permits a reached post-hit remove/transfer effect to survive the
  existing faint gate. `BattleEngineMoveEffects.cpp` remains the outer atomic
  move checkpoint and gained only descriptor-identity comparison.
- `BattleEffectExecutorTests.cpp`, `BattleItemRuleTests.cpp`, and
  `BattleAbilityItemContractTests.cpp` retain their existing executor, item-rule,
  and shared-contract test families. The new
  `BattleHeldItemMoveEffectTests.cpp` remains one cohesive R5 integration family
  across the shared catalog, engine, ledger, hook, reveal, event, and rollback
  fixtures.
- The 656-line `BattleEffectExecutorItemMoves.cpp` was reviewed at the 500-line
  trigger. Its one atomic resolver intentionally remains cohesive; splitting
  the ledger, mirror, hook, reveal, and event steps would fragment the single
  staged mutation workflow. Final code review accepted this as non-actionable.

Final evidence is rooted at
`Game/Saved/AutomationReports/R5-HeldItemMoves-Final-20260830-091817`.
The exact forced-Unity build passed; its build log SHA-256 is
`DE3D70FB5EB3C7C9D01DD1E5FEE376655E5EAEE85CA7F1953B158B9AA58A103C`,
and the linked DLL SHA-256 is
`37E73305CA4E5CD9D9881BF12E565B2C5E7B2AD49AB4319C77BA3B726E165ED1`.
The 12-test focused report and all 12 required affected filters contain 210
successful overlapping executions over 198 unique paths. Every aggregate and
per-test issue counter is zero. Counter, exact-path, and source-hash manifest
SHA-256 values are
`55412CA9E40EDAB0440403DD9C46E6DDC32CD41817C8F6456537F77A201AC165`,
`45090B742F815CA44431B0F96FEB4774C605330C7F840C252E87150C1AAC15BC`,
and `E2AFED5A8C2A8161FA11B65A5843641CED4D1F68E68354442744669331B2110A`.
Final `code-review` was APPROVED and `test-evidence-review` was
ADEQUATE/COMPLETE after all validated findings were fixed. The final R5 review
session performed no Git action; the accepted implementation was later
published in `7686395`. This was not independent R7, and no C10A row was
authored.

### R6 — Optional condition removal

Status: **COMPLETE — implementation and validation accepted 2026-08-30**.

R6 resolved B12 without introducing move-specific group code. The new
`OptionalIfAbsent` bit is accepted only for `RemoveCondition`. Runtime presence
is derived from the catalog condition family and the exact staged battler,
side, or field owner. Present conditions use their normal cleanup exactly once;
optional absence remains applied but stages no mutation and emits no condition
event or update. Legacy absence and malformed descriptors still fail.

The exact hand-authored code/test write set was:

- Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h
- Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp
- Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutorConditions.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleVolatileTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleFieldSideConditionTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleAtomicMoveEffectTests.cpp

Exactly seven focused `PokemonSolarus.Battle.C05B.C10Removal` identities cover
the generic executor contract, adapter/catalog validation, Rapid Spin, Defog,
Brick Break, and atomic rollback. Rapid Spin proves connected Substitute damage
before cleanup and Speed +1; Defog proves Evasion ordering and exactly 14
present removals; Brick Break proves three pre-damage screen removals through
Substitute while Protect preserves screens and HP.

The code-file organization review kept the existing executor coordinator,
condition executor, executor-test, volatile-test, and field/side-test
responsibilities; R6 added no new owner. The field/side helper remains local to
its fixture. The 1,034-line atomic test kept its required local catalog because
extracting it would create a tenth code/test path outside the approved set. The
998-line catalog and 869-line adapter stayed below the 1,000-line decision
threshold.

The post-review forced-Unity compile succeeded at
`Game/Saved/AutomationReports/R6-OptionalConditionRemoval-ReviewFix-20260830-161500/build-after-event-order-fix.log`,
SHA-256
`033D53201BD0A76865FE23C6C75AFDD5DF14A44E5BCF6B0508D5903CB808565D`.
The final evidence root is
`Game/Saved/AutomationReports/R6-OptionalConditionRemoval-Final-20260830-160508`.
The focused filter passed 7/7; C05B, C07B, C07C, C07D, C08B, C08C, and
ADR0002.3E6 passed serially with counts `42, 9, 8, 9, 20, 39, 18`. Across all
eight reports, 152 overlapping executions and 145 unique full-test paths
succeeded, with zero aggregate or per-test issue counters.

The final build log, linked editor DLL, counter manifest, exact-path manifest,
and source-hash manifest SHA-256 values are
`C109E74D8779882DE66D89B0F96F07F649C8263F25376912B3597762E97DFCE7`,
`BD672C763990C59FB4B2FB27B0BA95ABFCB698028F9A335ED0EEB86EEAAAB562`,
`2DE268F32848921228918BE3CFB85C5E10B436A8412E22A6A91943D03F50CF19`,
`10910887422FBF1B26E201A1B45C9B652F6C5BA11FF5FE1A977036E52FA70090`,
and `0A81A479400A85E1B4F8C99C19386722D2311447593C5813D8D6AB68BD8D8A80`.
The source manifest proves all nine source/test writes precede the final build
log. Final `code-review` was APPROVED and final `test-evidence-review` was
ADEQUATE/COMPLETE after all validated findings were fixed. The final R6 review
session did not itself perform a Git action; the accepted implementation was
later published in `74b1c1b`. This was not independent R7 at the time, and no
C10A row or asset was authored.

### R7 — Independent blocker gate

Status: **PASS — independent review accepted 2026-08-30**.

The independent session compared live code after R1 through R6 against all 62
rows using both `code-review` and `test-evidence-review`. Code review was
APPROVED, and test evidence was ADEQUATE/COMPLETE. The only blocking finding was
this document's stale self-contradictory status text; the live code and exported
test evidence had no blocker.

Fresh evidence is rooted at
`Game/Saved/AutomationReports/R7-IndependentGate-20260830-164042`. HEAD and
`origin/main` were both
`74b1c1b2d20b75e465c49d45dc193a0a74ecbcbd`. The requested forced-Unity build
configuration succeeded but was up to date, so it performed zero compiler
actions and is not represented as a fresh compilation. The 75-file source
manifest had zero mismatches, and the linked editor DLL matched the accepted R6
DLL at SHA-256
`BD672C763990C59FB4B2FB27B0BA95ABFCB698028F9A335ED0EEB86EEAAAB562`.

All 22 serial Automation reports were valid. They contained 746 overlapping
executions over 390 unique full-test paths; the full Battle filter succeeded
390/390. Aggregate and per-test issue counters were zero, and every expected
path was present exactly once in its requested report.

## 11. Completed C10A write boundary

The user granted exact C10A implementation approval. That session modified
only:

- Game/SourceData/Battle/Initial/species_forms.json
- Game/SourceData/Battle/Initial/natures.json
- Game/SourceData/Battle/Initial/moves.json
- Game/SourceData/Battle/Initial/abilities.json
- Game/SourceData/Battle/Initial/items.json
- Game/SourceData/Battle/Initial/conditions.json

Validate-only:

- Game/SourceData/Battle/Initial/type_chart.json
- every schema, adapter, catalog, runtime, and test file
- all existing Unreal Data Table assets
- both untracked ADR files

C10A stopped without expanding production scope and authored all 62 selected
moves.

C10A exclusions:

- display_names.json
- runtime_scenario.json
- import_initial_battle_data.py
- all uasset files
- all C++ and automated tests
- Cry for Help or reinforcement code/content
- Struggle as a normal row
- UI, HUD, animation, art, materials, textures, layout, styling, or other
  visual assets
- Game/Binaries, Game/Intermediate, Game/Saved as hand-edited paths
- roadmap/status documents before acceptance
- Git stage, commit, push, branch, reset, checkout, or history changes

## 12. Accepted C10B boundary and canonical package owner

The accepted C10B record is maintained in
`plan/battle_mechanics/11-canonical-proof-content.md`. Live inspection after
C10A found that the runtime display-name resolver requires a name for every
catalog species. C10B therefore added the six missing species display-name rows
and imported the display-name Data Table as the seventh approved asset. The
older six-asset expectation in this draft is superseded. The type chart and
runtime scenario remained validate-only and byte-identical.

C10B added only the pure validator, two Python test files, the importer
coordinator change, the display-name source change, one focused C++ test file,
and the narrow runtime-test update. The existing reflected row structs,
`FBattleDataTableAdapter`, `FBattleDefinitionCatalog`, and runtime loader remain
the production owners. The accepted import evidence is rooted at
`Game/Saved/AutomationReports/C10B-ImportAndCatalog-20260831-090312`. A later
approved encoding-only correction converted `Game/PokemonSolarus.uproject` to
semantically identical UTF-8 without BOM, with provenance at
`Game/Saved/AutomationReports/C10B-UProjectUtf8Normalization-20260831-095635`.
Final rebuilt validation and review evidence is rooted at
`Game/Saved/AutomationReports/C10B-ReviewRemediationFinal-20260831-110350`; the
catalog SHA-256 is
`94CDB260DD1129C61E80CF4087389F1DC0265E3DCEDF95E0E254EBBC6A7F3CBA`.

## 13. Preserved C10B validation contract

This section preserves the remediation-era validation contract that C10B later
satisfied. The live C10 and C11 records in
`plan/battle_mechanics/11-canonical-proof-content.md` and
`plan/battle_mechanics/12-integration-and-release-gate.md` supersede any
future-lane wording below.

No build or Automation run belongs to this documentation-only session.

New focused prefixes:

| Lane | New focused prefix |
|---|---|
| R1 targets | PokemonSolarus.Battle.C04B.C10Targets |
| R2 redirection | PokemonSolarus.Battle.C04B.C10Redirection |
| R3 ally modifier | PokemonSolarus.Battle.C05B.C10ActionModifiers |
| R4A hit qualifiers | PokemonSolarus.Battle.C05B.C10HitRules |
| R4B weather move rules | PokemonSolarus.Battle.C05B.C10WeatherMoveRules |
| R5 item moves | PokemonSolarus.Battle.C08C.C10HeldItemMoves |
| R6 removal | PokemonSolarus.Battle.C05B.C10Removal |
| C10B import/catalog | PokemonSolarus.Battle.C10B.ImportAndCatalog |

C10A itself remains a six-JSON-file data session and adds no Automation test.
It uses a timestamped read-only source-validation report identity such as
C10A-SourceContent-YYYYMMDD-HHMMSS. The Unreal Automation prefix begins in
C10B, after the import/integration tests exist.

Per-lane affected older filters:

| Lane | Minimum affected older filters |
|---|---|
| R1 | C02B, C04A, C04B, C05B, C06A, C06B, ADR0002.3E5 |
| R2/R3 | C04A, C04B, C05B, C06A, C06B, C07A, C07C, C07D, C08B, C08C, ADR0002.3E5, ADR0002.3E6 |
| R4A/R4B | C03A, C03B, C05A, C05B, C05C, C07B, C07C, C07D, C08B, C08C, ADR0002.3E6 |
| R5 | C02B, C04B, C05B, C06A, C06B, C07C, C07D, C08A, C08B, C08C, C09C, ADR0002.3E6 |
| R6 | C05B, C07B, C07C, C07D, C08B, C08C, ADR0002.3E6 |

Because the combined remediation changed shared target, engine, and executor
surfaces, the final R7 gate requested a forced-Unity
PokemonSolarusEditor Win64 Development build with:

- -ForceUnity
- -DisableAdaptiveUnity
- -BytesPerUnityCPP=1
- -NoUBA

R7 then ran the current shared-Battle matrix serially, one filter at a time:

- PokemonSolarus.Battle.ADR0002
- PokemonSolarus.Battle.C03A
- PokemonSolarus.Battle.C03B
- PokemonSolarus.Battle.C04A
- PokemonSolarus.Battle.C04B
- PokemonSolarus.Battle.C05A
- PokemonSolarus.Battle.C05B
- PokemonSolarus.Battle.C05C
- PokemonSolarus.Battle.C06A
- PokemonSolarus.Battle.C06B
- PokemonSolarus.Battle.C07A
- PokemonSolarus.Battle.C07B
- PokemonSolarus.Battle.C07C
- PokemonSolarus.Battle.C07D
- PokemonSolarus.Battle.C08A
- PokemonSolarus.Battle.C08B
- PokemonSolarus.Battle.C08C
- PokemonSolarus.Battle.C09A
- PokemonSolarus.Battle.C09B
- PokemonSolarus.Battle.C09C
- PokemonSolarus.Battle.Runtime
- PokemonSolarus.Battle

Use fresh unique generated report roots. For every exported index.json:

- discovered and performed counts must equal the session's pre-recorded
  expected manifest;
- all discovered paths must be under the requested prefix;
- every expected path must occur exactly once;
- succeeded must equal performed;
- succeeded-with-warnings, failed, not-run, and in-process must all be zero;
- every per-test warning and error count must be zero; and
- process exit code alone is never acceptance evidence.

New focused tests will change historical counts, so old aggregate counts must
not be copied forward as expected values. Discover and freeze the new exact
manifest from the rebuilt live binary.

C10B additionally requires:

- source JSON and imported Data Tables produce identical immutable values;
- exact counts of 8 species, 25 natures, 62 moves, 8 Abilities, 14 items,
  40 conditions, 18 types, and 324 type pairs;
- duplicate, missing, wrong-family, range, target, flag, and effect errors fail
  atomically with row/field diagnostics;
- a deterministic catalog summary/hash;
- proof that reimport cannot change a catalog already frozen for a battle; and
- pre/post hashes proving excluded JSON, assets, config, project, dirty ADRs,
  and unrelated source are unchanged.

No visual or actual-size PIE acceptance is claimed by this draft. C10 content
and Data Table behavior require Editor/Automation evidence. Blueprint lifecycle
or visual acceptance belongs only to a later package that explicitly claims
it, with the user's visual ownership preserved.

## 14. C10A completion and accepted C10B result

The technical preconditions for C10A are now satisfied:

- the accepted R0 decisions for B01, B08, B09, and B10 remain recorded and
  unchanged;
- B02 through B12 have reusable typed implementations and fresh evidence;
- no selected row requires a content-ID branch;
- the independent R7 gate passes against live source;
- the source extraction manifest is reproducible from approved pinned inputs;
- the only dirty paths are the preserved inventory plus explicitly approved
  remediation changes;
- the exact C10A six-file write boundary is rechecked.

The user granted the exact six-file write approval. The completed source slice
passed the validation defined in section 13 at
`Game/Saved/AutomationReports/C10A-Implementation-20260830-172323`: exact family
counts, pinned-source comparisons, unique IDs, references, ordered descriptors,
and the selected complex effect sequences all passed with zero errors. This is
source-data acceptance only; it does not claim imported Data Tables, Automation,
PIE, Blueprint lifecycle, or visual acceptance.

C10B subsequently supplied the imported-data proof at
`Game/Saved/AutomationReports/C10B-ImportAndCatalog-20260831-090312`. Exactly
seven approved Data Tables changed, both validate-only assets retained their
baseline hashes, Python passed 21/21, the forced-Unity Editor build succeeded,
and 12 serial Automation reports passed 187/187 with zero issue counters.
Source and two independent production loads produced the same catalog SHA-256:
`94CDB260DD1129C61E80CF4087389F1DC0265E3DCEDF95E0E254EBBC6A7F3CBA`.
The accepted final state additionally records the semantic-only `.uproject`
UTF-8 conversion at
`Game/Saved/AutomationReports/C10B-UProjectUtf8Normalization-20260831-095635`
and the final 150/150-action clean rebuild, battle-owned catalog-isolation
remediation, 187/187 matrix, and clean reviews at
`Game/Saved/AutomationReports/C10B-ReviewRemediationFinal-20260831-110350`.
No PIE, Blueprint lifecycle, visual acceptance, or C11 work is claimed.

## 15. Historical R0 session stop line

This R0 session updates this document and the B00B rules snapshot only. It does
not implement, build, run Automation, import, stage, commit, or approve any
remediation or C10 content.

This section records only the historical R0 stop boundary. It is superseded for
current status by sections 2, 10, and 14. R1 through R7, the source-extraction
gate, C10A source-row authoring, and C10B import/catalog acceptance are
complete. Current continuation is `C11A -> C11B`; C11A requires a fresh
implementation-approval task.
