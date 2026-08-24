# B00B Modern Rules Snapshot

Status date: 2026-08-20  
Status: Accepted B00B completion artifact  
Scope: Documentation and reference freezing only; no battle mechanics were
implemented or changed

## Gate

This file is the single modern-rules reference for the battle-mechanics
roadmap. The user accepted the supplementary evidence and the explicit Solarus
closures recorded below. B00B is complete. C01 was not started in the B00B
session and begins only in a later session.

Every rule below is one of:

- **Solarus**: an explicit decision copied from
  `docs/battle-system-interview-handoff.md` or a package that transcribes that
  handoff.
- **Pinned**: behavior extracted from one of the exact revisions below.
- **Stop**: the permitted sources do not establish the rule. The affected
  package must not guess it.

Pokemon Showdown, Smogon's damage calculator, and the capture disassembly are
community engineering references. Bulbapedia, the SacredPhoenix report, and
the Smogon discussion are also community references. They are not official
game sources. Where those sources remained incomplete, the accepted Solarus
rule is stated explicitly instead of being presented as verified Gen IX
behavior.

## Reproducible source lock

| Authority | Exact revision or version | Verification |
|---|---|---|
| Solarus battle handoff | SHA-256 `476040bcaf0cfdb9d7f97d3fba3ddc752f7760de252bcc7a8775f846a58c98a6` | Live workspace file |
| [Pokemon Showdown](https://github.com/smogon/pokemon-showdown/commit/34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d) | `34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d` | Detached checkout and `git rev-parse HEAD` |
| [Smogon damage-calc](https://github.com/smogon/damage-calc/commit/83807801012f0af3e2dbb543d6fd40b483b3ebab) | `83807801012f0af3e2dbb543d6fd40b483b3ebab` | Detached checkout and `git rev-parse HEAD` |
| [Scarlet 1.1.0 capture disassembly](https://gist.github.com/Lusamine/100831f809750a5f75951fedc331eb6d/31d6aa136883ab354f5e8151526ee40f07317be0) | parent revision `31d6aa136883ab354f5e8151526ee40f07317be0` | Parent and all ten linked function revisions fetched |
| [Epic UE 5.8 Data Driven Gameplay](https://dev.epicgames.com/documentation/unreal-engine/data-driven-gameplay-elements-in-unreal-engine) | UE 5.8.1, changelist `56057345` | Installed `Build.version` SHA-256 `d35d3d5f4de0636fbb62386b63dee8056889fb2baf199d012dc32ee77f7a2997` |
| Installed `Engine/DataTable.h` | UE 5.8.1 | SHA-256 `1644b15509198b370716c3a106193f3bc642f40ab646235526c69fb8db4a73a4` |

Supplementary Gen IX source lock authorized for obedience, special-command
placement, Revive, Full Heal, and X Attack only:

| Source | Exact pin | SHA-256 of fetched artifact | Use and limit |
|---|---|---|---|
| [Bulbapedia Obedience](https://bulbapedia.bulbagarden.net/w/index.php?title=Obedience&oldid=4554429) | `oldid=4554429`, raw wikitext | `b53f050d074c5c9e58d9364754f4ea1a2cd0c63449a8db0f8e70ab2576d2d97f` | Gen IX reference level, badge caps, and broad outcomes; its post-Gen-IV formula section is explicitly incomplete |
| [Bulbapedia Priority](https://bulbapedia.bulbagarden.net/w/index.php?title=Priority&oldid=4535978) | `oldid=4535978`, raw wikitext | `2502da8f8cadeca76ff9b2d3fbe916cb83b77629279251751928ca32624214b4` | Establishes switching, item use, and escape before moves; does not establish their relative order |
| [Bulbapedia Revive](https://bulbapedia.bulbagarden.net/w/index.php?title=Revive&oldid=4594350) | `oldid=4594350`, raw wikitext | `e650ef2f9d71ebf9be981d5f21485b61e11c90ba7197cc20ca774b5ab0c1d81a` | Fainted target and half-Max-HP effect; does not state odd-HP rounding |
| [Bulbapedia Full Heal](https://bulbapedia.bulbagarden.net/w/index.php?title=Full_Heal&oldid=4614360) | `oldid=4614360`, raw wikitext | `eec2a5f9863c102e070df707c942949a0e3e7de0c51bdf7135fccc57b0875549` | All non-volatile status conditions plus Confusion |
| [Bulbapedia X Attack](https://bulbapedia.bulbagarden.net/w/index.php?title=X_Attack&oldid=4595515) | `oldid=4595515`, raw wikitext | `056ec4a271f7b73f81e13525877e2d167856bccf63689e0ab6605fa97eebf31d` | Active target, two Attack stages, and withdrawal cleanup; does not establish use at `+6` |
| [SacredPhoenix obedience report](https://www.sacredphoenix.fr/res/Telechargements/PDF/GDD/Obedience%20and%20Traded%20Pok%C3%A9mon%20-%20vanilla%20mechanics_EN.pdf) | fetched PDF, 2026-08-20 | `1d53fcb0e807cc386abeb633d6d5556573b87dbe06831082ab2d22673356e7f1` | Diagnostic only: visually checked report labels its post-Gen-IV formula as a deduction, not a verified rule |
| [Smogon disobedience discussion](https://www.smogon.com/forums/threads/disobedience-mechanics-and-formula-since-gen-5.3733545/) | fetched HTML, 2026-08-20 | `a6f868f74c20043b1538a104d482d8570683b8cec01b3bc101be00e7c11c3a31` | Diagnostic only: requests validation of a hypothesis and supplies no verified Gen IX algorithm |

Key extracted-file hashes:

| Revision | File | SHA-256 |
|---|---|---|
| Showdown | `sim/battle-actions.ts` | `cb55b98e111926bc2f7bf62d2cadfd0d9c4bff81932336e5f3286a642d9fbb0d` |
| Showdown | `sim/pokemon.ts` | `c279b6225f4d39a5f9c626c81ba2aa9fe928ecdce34f4c617bbf3f567f201936` |
| Showdown | `sim/battle.ts` | `1d413692d0ed518e992593c0644a5456ab5497cbf3baf2542e65d9fba8d054b1` |
| Showdown | `sim/battle-queue.ts` | `116f55f3ba3811c142a8c511b19216dac707a7e689de7b26d6ec81666008818c` |
| Showdown | `sim/field.ts` | `b8a3f0b6f954d8a84f422de75d184242a503b7b216dfa44368dec6d525c6d0ac` |
| Showdown | `sim/dex-moves.ts` | `fe98743ca5f28096a9919590199a257f91a64f89fca24789151ebf942515e92f` |
| Showdown | `sim/prng.ts` | `e73e3a3b9c85c2fd96df4b4d5ef5893a7461e7c34a2c25b47a15daaae32b6856` |
| Showdown | `sim/side.ts` | `e0e7da80c8bf174b367fb473b5f5a69ba3baeeea5ab89ec430f19dcc426d916d` |
| Showdown | `data/conditions.ts` | `4b8d35e0237ba3001acc7f91b0b0be4dc29759214cb31fb0ead2aeb6b91970d8` |
| Showdown | `data/abilities.ts` | `60cd08e04a802b182cc9507a5514cd88300373dbb8a5833e3e049d27cf682942` |
| Showdown | `data/items.ts` | `b8ec6ef48590402e83502902f5205a798c02a72f35d710d35cd30902e08c3f4e` |
| Showdown | `data/moves.ts` | `30e36c2295ce6e2088cbdbebc41fb554c32a8d52be5635ace6067aaf1532ed11` |
| Showdown | `data/natures.ts` | `e05107d75d6cdb08e51e1dbd7c2365d1d3883f9c11acd2d63efe770a42ec9ee7` |
| Showdown | `data/typechart.ts` | `a5edf314e46d7f85c4017b9d074e4794c611321b007e98d0968774683f3d117c` |
| damage-calc | `calc/src/mechanics/gen789.ts` | `ab0fdd370ae0547e42ca6f7303329e2fcb441fd7eac84daa85c972bd83d5c78d` |
| damage-calc | `calc/src/mechanics/util.ts` | `7449f6711bc382a66f9f8cd053c74c111941f57b3755a5f4215825fc188da5e9` |
| damage-calc | `calc/src/stats.ts` | `b9b285e7655b508345f37f6d92a4c69dc2b6698cef6dd5be5440c87e80b4fa03` |

Capture-function hashes and exact child revisions:

| Function | Child revision | SHA-256 |
|---|---|---|
| `useBall` | `0013f550f466983bf4502f0b8038804443d7bc1d` | `01d567e81b289094a51ce958e91b9427a4f4e2f7e3df4ded73aa9fcc08849326` |
| `get_capture_value_coefficient` | `86d42ca526d4d89305e7b93779f22e642bc1b80d` | `c45628cb050816724ebcd6ee92c5a64812f3f4257192e1a75414a23cf4bbf51d` |
| `JudgeCapture` | `7823f1cf77e95135a1444d8eafefce02934e8a13` | `5f582d6af9c6839dfb47e453eca8a7b9f2b7ac459548c77ee8041e2db3e8f819` |
| `calcCaptureIndicator` | `4b964d5891a01951169e4604d2a8fb563e81e51c` | `e1c914f003c4fed9bd12e28aa46e68cf932ce02e0a57122a480f5d2e1ad28750` |
| intermediate ball ratio | `c9130c0f5449472c2d6d33eb003a2d7299d4363f` | `414ca9c6774e987d5213113aff5bf17b2d278cf141136ac8567893e0773d1184` |
| ball ratio | `327def25713aaba2766fc7d291b86e4159216f84` | `3a588aa7a3541345d4cdc40db8c6168240bafc917b24bbdb0801ae599dd7778e` |
| badge modifier | `fa38d739b2a70544dca36c360744f3d3b6e462a5` | `231fa7101d60c401416d2a76e4b91d72c8c9ed0ae93f3fa06156f9e0d0103fce` |
| critical capture | `1fcef05f41cae20ab4a4bdfff22d8ed0fcaa96e7` | `ac9a4b8e0405e51efe7376a02607a2f9ba6119eb3ea1003ec70c0ceb0543f913` |
| shake chance | `4348ca022fbacf69e3f0e4d0f7d6b3fee36e33ab` | `a3d969e0eee1425a79bb2a0d61ed46623516668701490b8d456f8282f8994794` |
| shake checks | `b15e08331186a28d620c7cf5b17e6f0250bd1d3d` | `630eff67bdaa81f315a77837bbf11ab7bf43c25244d47fb31dc51821eb297ac6` |

## Arithmetic and RNG notation

- `floor(x)` rounds toward negative infinity. All formulas here use
  non-negative operands unless stated otherwise.
- `trunc(x)` drops the fractional part.
- `PokeRound(x)` is `ceil(x)` only when the fractional part is greater than
  `0.5`; an exact `.5` rounds down.
- `RoundHalfUp(x) = floor(x + 0.5)` for non-negative `x`; an exact `.5`
  rounds up.
- `OF16(x)` is `x mod 65536` when `x > 65535`; otherwise it is `x`.
- `OF32(x)` is `x mod 4294967296` when `x > 4294967295`; otherwise it is `x`.
- Q12 fixed point uses `4096 == 1.0`.
- `RoundQ12(x, m) = floor((x * m + 2048) / 4096)` for non-negative integers.
- `U[a,b]` means one uniform inclusive integer draw. `U[n]` means
  `U[0,n-1]`.

Solarus uses one injected, seedable RNG stream. A trace entry is emitted for
every consumed draw with the rule identifier, bound, raw value, and result.
Rejected commands and ineligible checks consume no draw. Do not infer draw
eligibility only from an outcome being certain: the pinned `randomChance` and
`sample` helpers still consume a one-value `U[0,0]` draw when called with
denominator/list length one. Rules that special-case certainty before calling
RNG say so explicitly below. Within one action, draws occur in the exact
resolver order documented below; later draws are not pre-sampled.

## Permanent battle stats

Inputs are level `L`, base stat `B`, individual value `IV`, effort value `EV`,
and nature multiplier `N`.

```text
EVTerm = floor(EV / 4)

HP = floor(((2 * B + IV + EVTerm) * L) / 100) + L + 10
Other = floor((floor(((2 * B + IV + EVTerm) * L) / 100) + 5) * N)
```

If the species' base HP is exactly `1`, HP is exactly `1`. Validate `L` in
`1..100`, `B > 0`, each `IV` in `0..31`, each `EV` in `0..252`, and the six-EV
sum at no more than `510`. Invalid input is rejected; it is not clamped.

Nature multipliers are exactly `1.1` for the raised stat, `0.9` for the lowered
stat, and `1.0` otherwise. The final nature multiplication is floored. The 25
natures are:

| Nature | Raised | Lowered |
|---|---|---|
| Hardy, Docile, Serious, Bashful, Quirky | none | none |
| Lonely | Attack | Defense |
| Brave | Attack | Speed |
| Adamant | Attack | Special Attack |
| Naughty | Attack | Special Defense |
| Bold | Defense | Attack |
| Relaxed | Defense | Speed |
| Impish | Defense | Special Attack |
| Lax | Defense | Special Defense |
| Timid | Speed | Attack |
| Hasty | Speed | Defense |
| Jolly | Speed | Special Attack |
| Naive | Speed | Special Defense |
| Modest | Special Attack | Attack |
| Mild | Special Attack | Defense |
| Quiet | Special Attack | Speed |
| Rash | Special Attack | Special Defense |
| Calm | Special Defense | Attack |
| Gentle | Special Defense | Defense |
| Sassy | Special Defense | Speed |
| Careful | Special Defense | Special Attack |

Permanent stats are immutable battle-entry facts. Temporary stages and battle
modifiers never rewrite them.

## Stat stages and accuracy/evasion

All seven stages clamp to `-6..+6`. Attack, Defense, Special Attack, Special
Defense, and Speed use the stat ratio below, then floor the result. Accuracy
uses the combined stage `clamp(attackerAccuracy - defenderEvasion, -6, +6)`.

| Stage | Battle-stat ratio | Accuracy ratio |
|---:|---:|---:|
| -6 | 2/8 | 3/9 |
| -5 | 2/7 | 3/8 |
| -4 | 2/6 | 3/7 |
| -3 | 2/5 | 3/6 |
| -2 | 2/4 | 3/5 |
| -1 | 2/3 | 3/4 |
| 0 | 1/1 | 3/3 |
| +1 | 3/2 | 4/3 |
| +2 | 4/2 | 5/3 |
| +3 | 5/2 | 6/3 |
| +4 | 6/2 | 7/3 |
| +5 | 7/2 | 8/3 |
| +6 | 8/2 | 9/3 |

For a move with numeric accuracy `A`, calculate
`floor(A * numerator / denominator)`. Do not clamp the result to 100.

After a stage ratio is applied, non-stage stat modifiers use Q12. Chain them in
declared hook order from `M = 4096` using
`M = floor((M * Next + 2048) / 4096)`. Apply the final modifier with
`Stat = floor((Stat * M + 2047) / 4096)`, which is `PokeRound` for a positive
Q12 product: fractions above `.5` round up and exact `.5` rounds down. A rule
that says it applies directly before this chain, such as Sandstorm's Rock
Special Defense or Snow's Ice Defense, uses `PokeRound(Stat * 3 / 2)` first.

## Complete 18-type chart

Rows are attacking type; columns are defending type. Values are exact damage
multipliers. Stellar is excluded because Solarus has no transformation
gimmicks.

|Atk \\ Def|Normal|Fire|Water|Electric|Grass|Ice|Fighting|Poison|Ground|Flying|Psychic|Bug|Rock|Ghost|Dragon|Dark|Steel|Fairy|
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Normal|1|1|1|1|1|1|1|1|1|1|1|1|0.5|0|1|1|0.5|1|
|Fire|1|0.5|0.5|1|2|2|1|1|1|1|1|2|0.5|1|0.5|1|2|1|
|Water|1|2|0.5|1|0.5|1|1|1|2|1|1|1|2|1|0.5|1|1|1|
|Electric|1|1|2|0.5|0.5|1|1|1|0|2|1|1|1|1|0.5|1|1|1|
|Grass|1|0.5|2|1|0.5|1|1|0.5|2|0.5|1|0.5|2|1|0.5|1|0.5|1|
|Ice|1|0.5|0.5|1|2|0.5|1|1|2|2|1|1|1|1|2|1|0.5|1|
|Fighting|2|1|1|1|1|2|1|0.5|1|0.5|0.5|0.5|2|0|1|2|2|0.5|
|Poison|1|1|1|1|2|1|1|0.5|0.5|1|1|1|0.5|0.5|1|1|0|2|
|Ground|1|2|1|2|0.5|1|1|2|1|0|1|0.5|2|1|1|1|2|1|
|Flying|1|1|1|0.5|2|1|2|1|1|1|1|2|0.5|1|1|1|0.5|1|
|Psychic|1|1|1|1|1|1|2|2|1|1|0.5|1|1|1|1|0|0.5|1|
|Bug|1|0.5|1|1|2|1|0.5|0.5|1|0.5|2|1|1|0.5|1|2|0.5|0.5|
|Rock|1|2|1|1|1|2|0.5|1|0.5|2|1|2|1|1|1|1|0.5|1|
|Ghost|0|1|1|1|1|1|1|1|1|1|2|1|1|2|1|0.5|1|1|
|Dragon|1|1|1|1|1|1|1|1|1|1|1|1|1|1|2|1|0.5|0|
|Dark|1|1|1|1|1|1|0.5|1|1|1|2|1|1|2|1|0.5|1|0.5|
|Steel|1|0.5|0.5|0.5|1|2|1|1|1|1|1|1|2|1|1|1|0.5|2|
|Fairy|1|0.5|1|1|1|1|2|0.5|1|1|1|1|1|1|2|2|0.5|1|

For a dual-typed defender, apply the first stored defending type, then the
second, multiplying without intermediate rounding. An immunity makes the
result zero. Reject duplicate stored types at data validation. Type-based
major-status immunities are Fire to Burn, Electric to Paralysis, Ice to Freeze,
and Poison or Steel to Poison/Toxic.

## Moves, PP, hit checks, and damage

### PP and Struggle

With base PP `P` and explicit PP-Up count `u` in `0..3`:

```text
MaxPP = floor(P * (5 + u) / 5)
```

Moves marked as not accepting PP boosts keep `MaxPP = P`. Current PP validates
in `0..MaxPP`. Content must supply `u`; the engine does not silently assume a
competitive or fresh-move default.

At action execution, run pre-move gates first. Sleep, Freeze, Paralysis,
Confusion, Flinch, Recharge, Taunt, Disable, and similar gates that stop the
action before it begins consume no PP. If the move passes those gates, deduct
one PP before target modification, redirection, immunity, Protect, accuracy,
and move-effect checks. A later miss, immunity, no-target failure, or ordinary
move failure therefore keeps the PP spent.

**Solarus exception:** if a queued move's selected target was captured, cancel
the action before all pre-move gates and PP deduction. Do not redirect it. A
fainted single selected target instead redirects to the other living opponent
when that target class allows it.

When every selectable move has zero PP, Struggle is available. Struggle has no
PP, accuracy `true`, priority `0`, base power `50`, Physical category, typeless
damage behavior that bypasses ordinary type immunity, and recoil
`max(1, RoundHalfUp(BaseMaxHP / 4))`. Solarus requires the player to choose
Struggle's target in Doubles; it is not randomly selected.

For approved percentage move effects in Gen IX, Recover-style healing is
`RoundHalfUp(BaseMaxHP * numerator / denominator)`, capped at Max HP. Drain is
`RoundHalfUp(actual HP damage removed * numerator / denominator)`. Ordinary
recoil is `max(1, RoundHalfUp(total actual HP damage dealt * numerator /
denominator))`. Thus Giga Drain heals half the actual damage with half up,
Double-Edge recoils by one third of actual damage with half up, and Recover
heals half Base Max HP with half up. Substitute, item healing, residual damage,
and Life Orb recoil retain their separately stated floor rules.

### Hit pipeline and accuracy RNG

For each affected target, resolve in this order:

1. semi-invulnerable reachability;
2. Protect and other `TryHit` gates;
3. type immunity;
4. move/Ability/item immunity hooks;
5. accuracy/evasion modification and the accuracy roll;
6. protection-breaking effects;
7. damage/effect application, per-hit updates, and secondaries.

A semi-invulnerable target remains selectable. Reachability is evaluated when
the action resolves. Fly-style targets can be hit by Gust, Twister, Sky
Uppercut, Thunder, Hurricane, Smack Down, and Thousand Arrows; Gust and Twister
double their base power for this target state.

An accuracy value of literal `true` consumes no accuracy draw. Otherwise, after
all modifiers, consume `r = U[0,99]`; hit iff `r < EffectiveAccuracy`. Numeric
accuracy `100` still consumes the draw even though it cannot miss. Each target
of a spread move gets its own accuracy check in stored target order. A
multi-accuracy move gets the first check normally and one new check before each
later hit; stop drawing after the first failed hit.

### Critical hits

An ordinary move's critical stage defaults to `1`. Clamp the modified stage to
`0..4`.

| Critical stage | Result |
|---:|---|
| 0 | impossible; no draw |
| 1 | consume `U[0,23]`; critical on `0` |
| 2 | consume `U[0,7]`; critical on `0` |
| 3 | consume `U[0,1]`; critical on `0` |
| 4 | consume `U[0,0]`; guaranteed |

An explicit always-critical or never-critical move consumes no critical draw.
After a successful critical draw, the defender's critical-block hook can still
cancel the critical; the already-consumed draw is retained. A critical ignores
negative Attack/Special Attack stages on the attacker, positive
Defense/Special Defense stages on the defender, and Reflect, Light Screen, or
Aurora Veil. Its damage multiplier is `1.5`, floored at the documented damage
step.

### Exact damage sequence

The live calculator currently implements only the initial base expression. The
target formula for C05 is the pinned modern sequence below.

```text
LevelTerm = floor((2 * Level) / 5) + 2
PowerTerm = OF32(LevelTerm * Power)
AttackTerm = OF32(PowerTerm * Attack)
Quotient = floor(AttackTerm / Defense)
Base = floor(OF32(Quotient) / 50 + 2)
```

Then apply, in order:

1. if the move is a Doubles spread move currently hitting more than one valid
   target, `Base = PokeRound(OF32(Base * 3072) / 4096)`;
2. weather: matching Sun/Fire or Rain/Water uses `6144/4096`; weakened
   Sun/Water or Rain/Fire uses `2048/4096`; apply with `PokeRound`;
3. critical: `Base = floor(OF32(Base * 1.5))`;
4. random damage: consume `r = U[0,15]`, set `Factor = 100 - r`, then
   `Damage = floor(OF32(Base * Factor) / 100)`. This produces the 16
   equiprobable factors `100..85` and preserves the pinned raw-draw mapping;
5. STAB: ordinary matching original type uses Q12 `6144`; otherwise `4096`;
   if not `4096`, compute `Damage = OF32(Damage * StabMod) / 4096`, then
   `Damage = PokeRound(Damage)`;
6. type effectiveness: multiply the two defending-type values, then
   `Damage = floor(OF32(Damage) * TypeEffectiveness)`;
7. Burn: for a Physical move by a burned attacker, unless the move/Ability
   exception says otherwise, `Damage = floor(Damage / 2)`;
8. chain approved final modifiers in their declared order using
   `M = floor((M * mod + 2048) / 4096)`, starting at `M = 4096`, and clamp the
   chained value to `41..131072`;
9. return `OF16(PokeRound(max(1, OF32(Damage * M) / 4096)))`.

The approved final modifiers currently needed are screen/Aurora Veil
`2048/4096` in Singles or `2732/4096` in Doubles, and Life Orb `5324/4096`.
Matching screens do not stack with Aurora Veil. Critical hits ignore them.
All 16 random values are separate expected test results; tests must not use a
continuous `0.85..1.00` approximation. Smogon damage-calc enumerates the same
set in ascending factor order (`85 + i`); runtime replay uses Showdown's
`100 - r` raw-draw mapping above.

Damage is applied per hit. Run the immediate per-hit update hooks before the
next hit, so a berry, Focus Sash, Air Balloon, faint, or new threshold can
change later hits. Stop further hits and their RNG calls when their target or
source no longer permits the hit.

For the approved `2..5` multi-hit family, including Bullet Seed, the hit-count
draw occurs after the first accuracy check succeeds and before first-hit damage.
Consume one `U[0,19]`: values `0..6` select 2 hits, `7..13` select 3,
`14..16` select 4, and `17..19` select 5. This is the exact `35%, 35%, 15%,
15%` distribution. A fixed-hit move consumes no hit-count draw. Each reached
hit independently performs its critical and damage-random draws; a move marked
for per-hit accuracy also performs a new accuracy draw before each later hit.
Bullet Seed itself uses only the initial accuracy check. Stop all later hit
draws when a hit faints the target or the move can no longer continue.

### Secondary-effect RNG

For each eligible secondary descriptor, in descriptor order, consume one
`U[0,99]` and apply it when the value is below the stated percentage. Pinned
Showdown consumes this draw even for an explicit 100% secondary. Do not draw
for a descriptor that is ineligible because the hit/effect never reached it.
Process secondaries target by target in stored target order and after that
target's damage.

## Action queue, priorities, and ties

The turn command set is locked before action execution. Later Speed changes do
not re-sort the already-locked queue. Mandatory replacement is a separate
checkpoint and not a move action.

Use these descending Solarus command-class bands:

1. legal wild Run;
2. ordinary voluntary switch;
3. Bag use, including capture;
4. moves.

These bands are queue ordinals, not public move-priority values. Within the move
band, sort by integer move priority, highest first. Quick Claw success adds
fractional priority `+0.1` but does not change the move's public integer
priority. Within the same command band and the same applicable move-priority
and fractional-priority values, use the effective Speed of the active battler
that owns the action, highest first, or lowest first while Trick Room is active.
Then apply the Solarus tie rules:

1. for an exact cross-side tie, all player-side tied actions precede all
   opponent-side tied actions;
2. within one side's exact tie, consume one `U[0,1]` for the two tied active
   battlers and use it to choose their order. A partner-owned battler belongs
   to the player side for this rule.

If all four Double-Battle battlers tie, resolve the two player-side battlers in
their one-draw random order, then the two opponent-side battlers in their
one-draw random order. Do not consume a cross-side tie draw. A Single Battle
cross-side tie consumes no tie draw.

At queue lock, evaluate Quick Claw once for each eligible selected move in
stable side/position order. Consume `U[0,4]`; it succeeds on `0`. The draw stays
consumed even if an earlier action later faints or captures that battler. On
success, reveal Quick Claw and apply `+0.1`; on failure, do not reveal it.
Quick Claw does not make a negative-priority move equal to a priority-zero
move, and Trick Room still reverses Speed ordering inside the resulting
priority band.

Run and Bag/capture are explicit Solarus placements because the supplementary
source establishes only that switching, item use, and escape precede moves, not
their relative order. A Trainer-battle Run remains rejected before queue lock.
For a command that reaches its band, revalidate immediately before execution.
A selection that was illegal initially returns to selection and consumes no
action, resource, or RNG. A committed command made stale by an earlier queued
action is canceled and ends that committed action slot, but consumes no item
and no RNG.

For a legal Run, execute the Solarus Run calculation in the Run band. Success
ends the battle and cancels the remaining queue; legal failure consumes the
action and lets later bands continue. For a legal capture, consume the ball and
begin capture RNG only after execution-time validation. Success ends the battle
and cancels the remaining queue; failure consumes the ball/action and lets later
actions continue. For another legal Bag item, consume the item/action
immediately before its state mutation. Every later action observes the state
produced by earlier Run, switch, and Bag/capture actions.

### Target classes and redirection

Supported target classes are self, selected ally, selected opponent, any
selected battler, random legal opponent, user side, opponent side, both sides,
field, and a fixed spread set. Empty, fainted, captured, or removed positions
cannot be newly selected. A semi-invulnerable battler remains selectable.

After PP deduction, ordinary target modification/redirection may replace a
single target only with a currently legal target. If a selected single-target
opponent fainted before resolution, use the other living opponent when one
exists. If the selected target was captured, the earlier Solarus cancellation
applies instead: do not enter redirection and do not deduct PP. Spread moves
construct one stable affected-position set and include allies when the target
class declares friendly fire.

For a random-opponent class in Doubles, first construct the non-empty legal set
in stable position order, then consume `U[0,n-1]` and select that index. This
includes a one-candidate `U[0,0]` draw. A fixed opponent target in Singles is
not a random class and consumes no target draw. The player explicitly selects
Struggle's target in Doubles.

For a Roar-style random forced switch, build the living legal reserve list in
party-slot order. An empty list fails with no draw. A non-empty list always
consumes `U[0,n-1]`, including `U[0,0]` for one reserve, and switches to that
index.

Resource outcomes are fixed as follows:

| Situation | Action | PP/item | Move RNG |
|---|---|---|---|
| invalid selection before queue lock | not consumed | not consumed | none |
| committed Bag/capture command becomes stale before execution | committed action canceled | item retained | none |
| blocked Trainer Run | not consumed | none | none |
| legal failed wild Run | consumed | none | Run draw only |
| selected target was captured | committed action canceled | no PP | none |
| user fainted before action | committed action skipped | no PP | none |
| move began, then missed/failed | consumed | PP consumed | only reached checks |
| item blocked before legal use | not consumed | item retained | none |
| legal item use whose effect is prevented | consumed | item consumed | only reached checks |

## Deterministic trigger ordering

Each event handler carries these keys:

```text
phase -> canonical order -> canonical priority -> canonical suborder
      -> effective Speed when that event family is Speed-ordered
      -> side ordinal -> active-position ordinal -> effect creation ordinal
```

Higher canonical order/priority/suborder values run first where the pinned
event declares descending order; the event definition records the direction
instead of relying on a generic sort assumption. Exact action-Speed ties use
the Solarus rule above. Non-action handler ties do not spend RNG: use the stable
side, position, then creation keys. Switch-in hazards are a pinned special case:
handlers that otherwise tie run in the order their side conditions were first
created. Adding a later layer does not give the hazard a new creation ordinal.

After every damage or healing mutation, run immediate update handlers in the
same deterministic order. After every individual handler, drain queued faints
before continuing when the event family permits faint processing. This prevents
hash-map iteration or asset load order from deciding public behavior.

## Standard obedience

The supplementary Gen IX source establishes the reference-level and badge-cap
facts. Solarus applies them with this ownership scope:

- **Solarus scope:** Only a move action owned by the player-controlled Trainer
  is subject to the player's obedience cap. Partner- and opponent-owned actions
  do not use that cap. Switch, Bag, capture, and Run actions never perform an
  obedience check.
- **Pinned reference level:** For a Pokemon caught by its current player
  Trainer, the obedience reference level is its met level. For an outsider
  Pokemon, it is the level at which that Trainer received it in the current
  game. Later level gains do not change this stored reference level.
- **Pinned badge caps:** The cap by Gym Badge count is: `0 -> 20`, `1 -> 25`,
  `2 -> 30`, `3 -> 35`, `4 -> 40`, `5 -> 45`, `6 -> 50`, `7 -> 55`, and
  `8 -> all levels`. Reject a badge-count input outside `0..8`; do not clamp it.

The exact post-Gen-IV probability and outcome sequence is not established by
the sources. Solarus therefore uses the user's accepted deterministic override:

1. After confirming that the queued actor/action still exists, perform the
   obedience gate as the first move-action gate, before status/volatile action
   denial, PP deduction, target modification, or any RNG call.
2. If the stored obedience reference level is at or below the current cap, the
   Pokemon obeys and resolution continues without an obedience draw.
3. If it is above the cap, it always refuses the selected move. Consume the
   action, emit the public refusal result, and stop that action.
4. Refusal consumes no PP or Bag resource and no RNG. It never substitutes a
   different move or target, inflicts self-damage, applies Sleep, or adds
   Confusion.

This outcome is an explicit Solarus rule, not a claim that deterministic
refusal is standard Gen IX behavior.

## Major status

Only one major status may be present. A second major status attempt fails
without replacing the first. Type, terrain, Ability, Safeguard, and move hooks
are checked before assignment. Solarus uses classic Freeze, not Frostbite, and
does not use modern friendship combat bonuses or overworld status damage.

| Status | Application / action check | Residual and cleanup | RNG consumption |
|---|---|---|---|
| Burn | Type immunity: Fire. Physical damage is halved at the damage step unless an explicit exception applies. | End turn: `max(1, floor(BaseMaxHP / 16))`. Persists on switch. | none |
| Paralysis | Type immunity: Electric. Effective Speed is `floor(Speed / 2)` after the stage calculation. Before move, a full-paralysis result denies the action before PP. | Persists on switch. | Each eligible action consumes `U[0,3]`; deny on `0`. |
| Sleep | On application, set counter to `U[2,4]`. At each eligible action, decrement first; at zero, cure and allow the action. Otherwise deny before PP, except a move explicitly usable while asleep. | Therefore denies one through three action checks. Persists on switch with its counter. | One application draw; no per-turn sleep draw. |
| Freeze | Type immunity: Ice; Sun blocks application. A move flagged to thaw the user cures first and proceeds. Otherwise check natural thaw before move; failure denies before PP. A damaging Fire move or move flagged to thaw its target cures after reaching the target. | Persists on switch. | Each eligible frozen action consumes `U[0,4]`; thaw on `0`. No draw when a move-forced thaw applies first. |
| Poison | Type immunity: Poison and Steel. | End turn: `max(1, floor(BaseMaxHP / 8))`. Persists on switch. | none |
| Toxic | Same immunity as Poison. Set toxic stage to `0`. | At each toxic residual, increment stage, clamp to `15`, then deal `max(1, floor(BaseMaxHP / 16)) * stage`. On ordinary switch-out, clear the toxic stage; re-entry starts again at stage `0` while Toxic itself persists. | none |

Residual damage is direct condition damage, not move damage. Magic Guard can
block it. Faint processing follows each applied residual before the next
battler's residual handler. On faint, the fainted state replaces and clears the
prior major status, and all battle-only effects clear, matching the explicit
Solarus faint-presentation decision.

## Approved volatile conditions

Unless a rule below says otherwise, approved volatiles clear on ordinary
switch-out, capture, or faint. Their duration counters decrement at the pinned
residual/action checkpoint before deciding expiration; when decrement reaches
zero, remove the volatile and do not run its effect at that checkpoint.

### Confusion

On application, set duration to `U[2,5]`. At the action gate, decrement first;
at zero, cure and continue. Otherwise consume `U[0,99]`; self-hit when the value
is below `33`. A self-hit cancels the selected action before PP and deals
typeless Physical confusion damage with base power `40`, using the confused
Pokemon as both attacker and defender. Substitute does not receive self-hit
damage.

Misty Terrain prevents Confusion for grounded Pokemon, and Safeguard prevents
it when applied by an opponent unless the applying move bypasses Safeguard.

### Flinch

Flinch lasts for the current action opportunity. Its before-move gate denies
the action and consumes no PP, then removes Flinch. It has no RNG after it has
been applied and cannot deny a battler that has already acted.

### Protect

Protect has priority `+4` and literal accuracy `true`. It requires the user to
have a queued action. The first consecutive use succeeds without a draw and
starts a chain counter at `3`. Each immediately consecutive attempt consumes
`U[0,counter-1]` and succeeds only on `0`; after the attempt, multiply the
counter by `3`, capped at `729`. A failed attempt clears the chain. A turn
without an eligible consecutive Protect also clears it.

On success, block effects whose move flags say Protect applies. Moves with an
explicit bypass or protection-breaking flag follow that flag. Protect is
checked before type immunity and accuracy, so a protected target causes no
accuracy draw.

### Leech Seed

Grass targets are immune. At end turn, if the source slot currently has a
living recipient, deal `max(1, floor(TargetBaseMaxHP / 8))` to the seeded
target, then heal that recipient by the actual HP removed. If the source slot
has no living recipient, do not damage or heal. Leech Seed clears when the
seeded target switches or faints, not merely when the original source leaves.

### Partial trapping and ordinary trapping

Partial trapping sets its counter to `U[5,6]`, producing four or five residual
ticks under the decrement-before-effect rule. Each tick deals
`max(1, floor(BaseMaxHP / 8))`, and the target cannot voluntarily switch. It
ends early if the binding source is no longer active/living. Grip Claw and
Binding Band are outside the approved proof set and must not be silently
assumed.

An ordinary trapping effect blocks voluntary switch while its live source and
eligibility remain. Ghost-type battlers retain their natural immunity to this
ordinary trapping restriction. Capture/faint cleanup still removes the
volatile.

### Taunt, Encore, and Disable

- Taunt normally has duration `3`; if applied after the target has already
  acted in that turn, its stored duration is one greater so it blocks the same
  number of future action selections. It rejects Status-category moves during
  selection and again at the before-move gate. It consumes no RNG.
- Encore has duration `3`. It requires a valid last move that has current PP
  and is not marked as unencoreable. It locks selection to that move. End
  Encore when the move becomes invalid or reaches zero PP. It consumes no RNG.
- Disable has duration `5`. It requires the target's valid last move, excluding
  Struggle, and blocks that move at selection and before-move priority `7`.
  End it if the move becomes invalid or has no PP. It consumes no RNG.

### Substitute

Creating a Substitute costs `floor(MaxHP / 4)` HP and creates that many
Substitute HP. It fails when `MaxHP == 1` or current HP is not strictly greater
than the cost. The cost is not damage and cannot faint the user.

Ordinary opposing move damage is applied to Substitute HP first. Excess does
not spill into the owner unless the move explicitly says it bypasses
Substitute. Opposing status/effect applications are blocked unless their flags
or Infiltrator-style hook bypass Substitute; self effects are not blocked.
Drain and recoil use actual Substitute damage. Remove Substitute at zero HP,
switch, capture, or faint.

### Charging, Fly-style semi-invulnerability, and recharge

A two-turn move's first execution pays PP, stores the move/target lock, and
adds its charge state. Its second action executes without a second PP payment.
If the charge is aborted by switch, capture, faint, or an explicit cancel hook,
clear the stored state. Fly-style charge also adds the semi-invulnerable state
and follows the reachability list in the hit pipeline.

Recharge is added after the move that requires it resolves. At the next action
gate it denies the action before PP, then removes itself. It consumes no RNG.

## Weather, terrain, hazards, screens, rooms, and side conditions

One weather and one terrain may exist at a time. A different one replaces the
current state; trying to set the identical state fails rather than refreshing
it. Ordinary duration `5` includes the turn of creation because end-turn
residual processing decrements it. The common extender items exist as hooks but
are outside the approved proof item set; if later admitted, they set duration
to `8`.

`Grounded` means not Flying-type, not made airborne by Levitate or Air Balloon,
and not in a semi-invulnerable airborne state, after all suppression hooks.
Excluded mechanics such as Gravity or Iron Ball are not inferred.

### Weather

| Weather | Duration | Exact approved effects |
|---|---:|---|
| Sun | 5 | Fire move damage `6144/4096`; Water `2048/4096`; blocks Freeze application. |
| Rain | 5 | Water move damage `6144/4096`; Fire `2048/4096`. |
| Sandstorm | 5 | Rock-type Special Defense `PokeRound(stat * 3 / 2)` directly before the ordinary defense-modifier chain; end-turn `max(1, floor(BaseMaxHP/16))` to non-Rock/Ground/Steel battlers unless another approved immunity blocks it. |
| Snow | 5 | Ice-type Defense `PokeRound(stat * 3 / 2)` directly before the ordinary defense-modifier chain; no residual damage. |

Drizzle establishes Rain through the same one-weather slot. No transformation,
primal weather, or overworld weather effects are in scope.

### Terrain

| Terrain | Duration | Exact approved grounded effects |
|---|---:|---|
| Electric | 5 | Prevent Sleep; grounded attacker's Electric move power uses `5325/4096`. |
| Grassy | 5 | Grounded attacker's Grass move power uses `5325/4096`; Earthquake, Bulldoze, and Magnitude against a grounded defender use `2048/4096`; end turn heal `max(1, floor(BaseMaxHP/16))`. |
| Misty | 5 | Prevent major status and Confusion; Dragon move damage against a grounded defender uses `2048/4096`. |
| Psychic | 5 | Grounded attacker's Psychic move power uses `5325/4096`; block an opposing move whose resolved priority is greater than `0.1` when it targets a grounded defender. A priority-0 move raised only by Quick Claw to `0.1` is not blocked. It does not block allied/self targets or an airborne defender. |

### Entry hazards

Hazards persist until their side is explicitly cleared or the battle ends.
Heavy-Duty Boots bypass all approved entry hazards. Process multiple hazards in
their first-creation order, with faint/update processing after each.

| Hazard | Layers | Switch-in effect |
|---|---:|---|
| Spikes | 1..3 | Grounded entrant loses `1/8`, `1/6`, or `1/4` of Base Max HP respectively, floored with minimum 1. |
| Toxic Spikes | 1..2 | Grounded Poison entrant removes the condition before status application. Grounded Steel entrant is unaffected. Otherwise apply Poison at one layer or Toxic at two, subject to status prevention. |
| Stealth Rock | 1 | Entrant loses `floor(BaseMaxHP * RockEffectiveness / 8)`, minimum 1 when effectiveness is nonzero. |
| Sticky Web | 1 | Grounded entrant receives Speed stage `-1`, subject to stage-prevention hooks. |

### Screens and side conditions

Reflect, Light Screen, and Aurora Veil last `5` turns. Reflect covers Physical
damage; Light Screen covers Special; Aurora Veil covers both but can be created
only during Snow. Matching screen effects do not stack with Aurora Veil.
Critical hits and an approved infiltrating hook ignore them. Their damage
modifier is `2048/4096` in Singles and `2732/4096` in Doubles.

Tailwind lasts `4` turns and doubles effective Speed after the ordinary Speed
calculation. Safeguard lasts `5` turns and blocks major status and Confusion
applied by an opponent; self-applied status is allowed, and an explicit
infiltrating move bypasses it. Mist lasts `5` turns and blocks negative stat
stages applied by an opponent, with the same explicit infiltrating bypass.

### Rooms

Each room lasts `5` turns. Using the same room move while it is active removes
it instead of resetting it.

- Trick Room has move priority `-7` and reverses only the effective-Speed
  comparison inside an otherwise equal action-priority band. Lower effective
  Speed acts first; exact ties still use Solarus tie rules.
- Magic Room suppresses held-item effects without deleting or consuming the
  item. When it ends, an unconsumed held item becomes active again.
- Wonder Room swaps Defense with Special Defense, including the corresponding
  temporary stages, only for defensive-stat queries. It does not rewrite the
  stored permanent stats or stage arrays.

## Approved Abilities

An Ability remains owned by its battler across switching. Entry Abilities run
again on each legal entry. Suppression makes the effect inactive without
deleting ownership. Reveal the full Ability name only when its official public
trigger occurs; later activations may emit a shortened repeat fact. A hidden
non-activation must not leak the Ability.

| Ability | Frozen trigger and effect | Reveal / suppression / cleanup |
|---|---|---|
| Blaze | During Attack or Special Attack query for a Fire move, if current HP is at most one third of Max HP, multiply the queried offensive stat by `1.5` through the stat-modifier chain. | Reveal when the boost affects a public damage result. No duration; threshold is rechecked per hit. Not breakable by Mold Breaker in the pinned flags. |
| Overgrow | Same as Blaze for a Grass move. | Same lifecycle as Blaze. |
| Intimidate | On entry, visit adjacent opponents in stable position order and attempt Attack stage `-1` on each. Substitute blocks that target's drop. | Reveal once at the first adjacent target attempt, even if all attempts are blocked. Entry event only; no stored duration. |
| Levitate | Makes the holder airborne, immune to Ground moves, and ineligible for grounded hazards and terrain effects. | Reveal only when Ground/grounded logic publicly needs it. It is marked breakable and is ignored by a Mold Breaker move. |
| Drizzle | On entry, set Rain through the one-weather slot for duration `5`. It replaces a different weather; identical Rain is not refreshed. | Reveal as the weather source. The resulting weather persists after the source leaves until duration/replacement/removal. |
| Speed Boost | At end turn, if the holder has completed at least one active turn, raise Speed one stage. Its residual key is order `28`, suborder `2`. | Reveal on the public stage change. No draw. It does not trigger on the entry turn when `activeTurns == 0`. |
| Magic Guard | Reject damage whose source effect type is not `Move`. Direct move damage remains. | Reveal when it prevents public damage. It blocks Burn/Poison/Toxic, weather, Leech Seed/partial-trap damage, Life Orb recoil, and other indirect approved damage; it does not block Substitute HP cost. It is not marked breakable. |
| Mold Breaker | On entry, reveal immediately. For each move by the holder, ignore defender Ability hooks marked breakable. | It suppresses the breakable hook for that move only; it does not remove the Ability. In the approved set this is directly relevant to Levitate. |

## Approved held items

Held-item ownership, current held item, original held item, consumed state, and
temporarily suppressed state are separate facts. Ordinary switching never
restores a consumed item. Magic Room suppresses effects but does not consume or
remove the item. A successful capture records the target's original/current
held-item state in the pending captured snapshot.

All fractional healing below is floored, with a minimum of one when a positive
heal is eligible, and capped at current Max HP.

| Item | Frozen trigger and effect | Consumption / cleanup |
|---|---|---|
| Leftovers | End turn, residual order `5`, suborder `4`: heal `max(1, floor(BaseMaxHP/16))`. | Not consumed. Does nothing at full HP. |
| Sitrus Berry | At each immediate update, if current HP is at most half Max HP and healing is permitted, heal `max(1, floor(BaseMaxHP/4))`. | Consume immediately before healing. Per-hit update means it can trigger between multi-hits. |
| Lum Berry | Immediately after status/update, if the holder has a major status or Confusion, cure its one major status and remove Confusion. | Consume once before cures. One activation may cure both the major status and Confusion. |
| Focus Sash | During direct move-damage adjustment, if the holder is at full HP and that hit would reduce it to zero, consume and set that hit's damage to `CurrentHP - 1`. | One use. It does not protect Substitute HP or indirect damage. A later hit of the same move may faint the holder. |
| Life Orb | Final damage modifier `5324/4096` for a damaging move. After the move, if source and target differ and no forced-switch result suppresses the hook, deal `max(1, floor(BaseMaxHP/10))` indirect recoil. | Not consumed. Recoil occurs after the move's damage/secondaries and is blocked by Magic Guard. |
| Choice Band | During Physical Attack query, multiply Attack by `1.5`. During move modification, lock to that move ID. Other moves are disabled while the item is active; Struggle remains permitted. | Lock clears on switch, item loss, or suppression; suppression does not delete the item. A different attempted move is stopped before PP. |
| Heavy-Duty Boots | Bypass all approved entry-hazard handlers. | Not consumed. Suppression makes hazards active again for an entry during suppression. |
| Air Balloon | While active, make the holder airborne and Ground-immune. Reveal on entry. Any damaging hit pops it, including damage absorbed by Substitute. | Remove on the first damaging hit; a non-damaging or fully blocked move does not pop it. |
| Quick Claw | For an eligible selected move whose current priority is at most zero, use the queue-lock `1/5` draw documented above; success adds fractional priority `+0.1`. | Not consumed. Reveal only on success. |

## Battle items and Bag actions

### Frozen Solarus rules

- Hyper Potion heals exactly `120` HP, capped at Max HP. It can target an active
  or reserve non-fainted Pokemon owned by that Trainer. A full-HP target is
  rejected before action lock and consumes no item or action.
- Only the player-owned Trainer may use a Poke Ball, only in a capture-legal
  wild encounter. Select exactly one living wild target. Validate target,
  encounter policy, count, and combined party/storage capacity including
  pending captures before consumption. A legal failed capture consumes one ball
  and the action; a blocked attempt consumes neither.
- At most one Bag item is selected for each Trainer action. Partner Trainers
  have separate Bags and cannot use the player's items or capture.

### Revive

Revive targets one fainted Pokemon in the acting Trainer's own party. Restore
exactly `max(1, floor(MaxHP / 2))` HP. The `floor` rule for odd Max HP is an
explicit Solarus closure; the pinned item reference states only half Max HP.
Fainting has already cleared the prior major status and battle-only effects, so
Revive restores neither. A revived reserve does not gain an action that turn.

Revalidate the target in the Bag band. If it is no longer fainted, cancel the
committed action with no item or RNG consumption. Otherwise consume the Revive
and action immediately before the HP mutation. Revive uses no RNG.

### Full Heal

Full Heal targets one living Pokemon in the acting Trainer's own party that has
a major status, Confusion, or both. It cures Burn, Paralysis, Sleep, Freeze,
Poison, or Toxic and removes Confusion. Curing Toxic also clears its toxic-stage
counter. It removes no other volatile condition.

Reject a target with neither a major status nor Confusion before queue lock. At
execution, revalidate that at least one curable condition remains; otherwise
cancel the committed action and retain the item. For a legal execution, consume
the Full Heal and action immediately before applying all eligible cures. Full
Heal uses no RNG.

### X Attack

X Attack targets the acting Trainer's currently active Pokemon and raises its
Attack stage by `2`, clamped to the already-frozen `+6` cap. A target at `+5`
therefore reaches `+6`. Ordinary switch cleanup removes the stage change.

Reject selection when the target is already at `+6`; this consumes neither the
item nor the action. Revalidate active ownership and the stage immediately
before execution. If an earlier action removed the target, switched it out, or
raised it to `+6`, cancel the committed action and retain the item. Otherwise
consume the X Attack and action immediately before changing the stage. X Attack
uses no RNG.

## Run, capture, reinforcement, and wild fleeing

### Solarus Run

Trainer encounters reject Run immediately with no action or RNG consumption.
For a legal wild attempt, use the acting player Pokemon and the living wild
Pokemon in the leftmost occupied opponent slot. Use their permanent unmodified
Speed values; ignore stages, Paralysis, Ability, item, weather, terrain, and
other temporary modifiers. Reject either calculated Speed below `4` before the
formula and consume no action/RNG.

```text
Denominator = floor(WildSpeed / 4)
F = floor((PlayerSpeed * 32) / Denominator) + 30 * C
```

`C` starts at `1`, increments only after a legal failed Run, and never resets
during the battle. If `F > 255`, Run succeeds without a draw. Otherwise consume
`R = U[0,255]`; succeed iff `R < F`. A legal failure consumes the action and
then increments `C`.

### Cry for Help

**Status: Freeze until call by user.** This subsection is retained as historical
design context only. Do not implement, remove, refactor, or test Cry for Help or
wild reinforcement until the user explicitly calls for it. Existing related
code and replay fields remain unchanged. This frozen mechanic is not part of
C09B acceptance and does not block C09C.

Cry for Help is legal only when the second wild active slot is empty and no
previous call has succeeded in that battle. The attempt consumes the caller's
action and PP. Consume `U[0,99]`; succeed when the value is below `80`. Failure
leaves the slot empty. Success creates the configured reinforcement in the
second slot and permanently closes further successful calls. The summon cannot
act until the next turn and may later be captured.

### Configured wild-opponent fleeing

The default policy is disabled. An explicit species/encounter policy must name
the trigger, eligibility predicate, and one probability mode: `Never`,
`Always`, or `Chance(numerator, denominator)`. The engine must not invent a
species flee chance. `Chance` validates `0 < numerator < denominator`, consumes
`U[0,denominator-1]`, and flees iff the result is below the numerator. `Never`
and `Always` consume no draw.

A legal flee consumes that wild battler's action, removes only that battler,
and cancels its later queued action without PP. If another wild opponent lives,
continue the battle. If none lives, end with `Escape`, cause `OpponentFled`, not
ordinary Victory.

### Scarlet 1.1.0 capture inputs

Freeze these immutable inputs at action execution:

- target species catch rate, current HP, Max HP, level, major status, and the
  Poke Ball target classification needed for special ball handling;
- acting/player level, badge count, and the badge-level capture snapshot;
- selected ball ID and Q12 ball multiplier;
- caught-species count, critical-capture enabled flag, Catching Charm flag;
- optional capture-value coefficient, caught-count HP-component modifier flag,
  must-capture flag, and every special setup flag used by ball handling;
- the one battle RNG stream position.

The canonical Solarus proof uses a normal, non-Ultra-Beast species, Poke Ball
multiplier `4096`, capture coefficient `4096`, critical capture enabled,
Catching Charm false, must-capture false, and all optional setup modifiers
false. Unsupported special setup combinations are rejected rather than mapped
to a guessed neutral value.

### Capture indicator

Let `M = MaxHP`, `H = CurrentHP`, and all following values be non-negative
integers unless explicitly converted to floating point.

```text
HPQ12 = (3 * M - 2 * H) << 12
```

If the explicit caught-count HP-component flag is enabled, choose Q12 modifier:

| Caught species | Modifier |
|---:|---:|
| 0..30 | 1229 |
| 31..150 | 2048 |
| 151..300 | 2867 |
| 301..450 | 3277 |
| 451..600 | 3686 |
| 601+ | 4096 |

Then `HPQ12 = RoundQ12(HPQ12, modifier)`. Multiply by the species catch rate,
including an explicitly supported Heavy Ball catch-rate adjustment if that ball
is ever admitted. For the approved Poke Ball and normal species, the ball
multiplier is `4096` and no catch-rate adjustment applies.

Apply the ball multiplier and badge modifier with Q12 rounding, then integer
divide by `3 * M`:

```text
AfterBall = RoundQ12(SpeciesCatchRate * HPQ12, BallMultiplierQ12)
AfterBadge = RoundQ12(AfterBall, BadgeModifierQ12)
A = floor(AfterBadge / (3 * M))
```

Badge modifier construction uses target level and thresholds
`[25,30,35,40,45,50,55,60,100]`. Start with float `1.0` at the index for the
badge count; for each remaining threshold still below target level, multiply
by float `0.8`. Convert the final positive float to Q12 by
`floor(value * 4096 + 0.5)`. More than eight badges produces Q12 `4096`.

For target level `L <= 13`, next apply the low-level bonus:

```text
A = floor(((36 - 2 * L) * A) / 10)
```

Apply one status modifier with `RoundQ12`:

| Status | Q12 modifier |
|---|---:|
| Sleep or Freeze | 10240 |
| Poison, Toxic, Burn, or Paralysis | 6144 |
| none | 4096 |

Finally apply `CaptureCoefficientQ12` with `RoundQ12`. A configured positive
floating coefficient converts to Q12 by `floor(value * 4096 + 0.5)`; a
non-positive configured value uses `4096`. The disassembly also contains an
unidentified special-mode branch returning coefficient `410` when the player's
level is below the target's. Solarus must reject that mode until its setup flag
is identified; it is not part of the neutral proof.

The resulting `A` is the capture indicator. `255 * 4096 == 0xFF000` is the
guaranteed-shake threshold, but the critical-capture check below occurs first.

### Critical capture

If critical capture is disabled, set `critical = false` and consume no draw. If
enabled and caught-species count is below `31`, also return false without a
draw. Otherwise choose Q12 modifier:

| Caught species | Modifier |
|---:|---:|
| 31..150 | 2048 |
| 151..300 | 4096 |
| 301..450 | 6144 |
| 451..600 | 8192 |
| 601+ | 10240 |

Double that integer when Catching Charm is present. Then:

```text
CappedA = min(A, 255 * 4096)
Q = RoundQ12(CappedA, CriticalModifierQ12)
Threshold = floor(floor(Q / 6) / 4096)
CriticalRoll = U[0,255]
critical = CriticalRoll < Threshold
```

For every enabled eligible check, the critical draw is consumed even when
`A >= 255 * 4096` and ordinary capture will be guaranteed.

### Shake threshold and RNG

If `A < 255 * 4096`, reproduce the disassembly's conversion boundaries:

```text
a = A / 4096.0                         // exact Q12 decomposition to double
QRatio = floor((255.0 / a) * 4096.0 + 0.5)
RootInput = QRatio / 4096.0
RootQ12 = floor(powf(float(RootInput), 3.0f / 16.0f) * 4096.0f + 0.5f)
Root = RootQ12 / 4096.0
Scaled = floor((65536.0 / Root) * 4096.0 + 0.5)
B = floor(Scaled / 4096)
```

Use IEEE single-precision `powf` at the marked step; replacing it with an
all-double algebraic expression can change a boundary vector.

For a critical capture, make exactly one shake check. Otherwise make exactly
four. Each check consumes `U[0,65535]` and passes iff the value is below `B`.
Stop at the first failure; do not pre-consume later checks. Capture succeeds
only after every required check passes. The public visual shake count caps at
three even though ordinary success used four checks.

If `A >= 255 * 4096`, consume no shake draw and capture succeeds; visual count
is one when the earlier critical check succeeded, otherwise three. A
must-capture mode bypasses the indicator, critical, and shake calculations,
consumes no capture RNG, succeeds, and reports three visual shakes.

### Solarus capture result

On failure, retain the target and consume the legal action and one Poke Ball.
On success, remove only the selected target; cancel that target's queued action
and every queued move specifically targeting it, with no redirection and no PP
for those canceled moves. Store a pending captured snapshot containing HP,
major status, PP, and original/current held-item state. It cannot enter the
same battle. Capturing the last wild opponent yields `Victory` with capture
cause; earlier successful captures remain in the final result even if the
player later escapes or loses.

## Action, faint, replacement, end-turn, and outcome checkpoints

Solarus freezes this high-level sequence:

1. Validate typed decisions without consuming PP, items, actions, or RNG.
2. Lock the normal-turn queue, evaluate eligible Quick Claw hooks, and resolve
   action ties exactly once.
3. Execute legal wild Run, voluntary switches, Bag/capture, and moves in that
   frozen command-class order. Revalidate each command at its documented
   execution checkpoint before consuming resources or RNG.
4. Before an action, skip a fainted/captured/inactive actor without PP. Apply
   the captured-selected-target cancellation before pre-move gates and PP.
5. For a move, run before-move gates, deduct PP, determine/redetermine targets,
   then run Protect/immunity/accuracy/hit/effect processing.
6. After each hit: emit HP change, run immediate update hooks, enqueue and
   process required target faint facts, then continue only if another hit is
   still legal.
7. After direct damage, resolve linked drain/recoil and their own HP/faint
   checkpoints. A target's damage/faint checkpoint precedes linked drain or
   recoil. Then run remaining after-move and action-completion hooks.
8. Complete the remaining locked normal-turn actions Solarus requires before a
   faint-driven mandatory replacement. A battler that fainted before its turn
   is skipped with no PP.
9. Evaluate terminal state before requesting replacement. If not terminal,
   request all required replacements together; a lone reserve for two empty
   positions enters the left position. Mandatory replacement consumes no next-
   turn action.
10. In an eligible Casual Single Trainer battle, offer the separate, no-action
    Shift decision before the opponent's mandatory replacement. Decline means
    Set. Wild, Double, partner Double, and forced-Set encounters never offer it.
11. Process end-turn handlers. If end-turn effects create faints, finish the
    current ordered handler group, process faints, re-evaluate terminal state,
    then request the new mandatory replacements after the residual phase.
12. If still active, expire turn-scoped state, advance the turn, and request
    the next decisions.

Approved residual handlers use the pinned low-to-high order key. The relevant
cross-family order is:

| Order | Approved handlers |
|---:|---|
| 1 | weather upkeep and weather effects, including Sandstorm damage |
| 5 | Grassy Terrain heal at suborder 2, then Leftovers at suborder 4 |
| 8 | Leech Seed |
| 9 | Poison and Toxic |
| 10 | Burn |
| 13 | partial trapping |
| 28 | Speed Boost at suborder 2 |
| default-last | duration-only field/side/volatile expiry, using stable effect creation order |

Run HP/faint/update processing after each individual mutation, not after the
whole table row. Sitrus and Lum are immediate update hooks rather than entries
in this residual list.

When multiple battlers reach zero HP from one spread/effect resolution, assign
one simultaneous-group ID, then emit individual faint facts in stable side and
position order. Determine the terminal result only after every faint and linked
effect in that group is known. **Solarus exception:** if both sides lose their
final usable Pokemon in the same simultaneous group, the result is player
`Defeat`.

Terminal outcomes are `Victory`, `Defeat`, `Escape`, `ScriptedEnd`, or
`Abandoned`, with capture, opponent-fled, and partner Team Victory represented
as typed causes. Terminal state rejects all later decisions and consumes no
more battle RNG.

## Unreal Engine 5.8 Data Table boundary

Epic's UE 5.8 documentation establishes that a Data Table row struct inherits
`FTableRowBase`, the first CSV column supplies the row `Name`, subsequent
columns map one-to-one to reflected row fields, and `UDataTable::FindRow<T>`
returns a row pointer. Epic also explicitly warns that returned table rows must
not be cached beyond local function scope because reimport may refresh data and
invalidate pointers.

Solarus therefore freezes this adapter boundary:

1. Unreal-facing `FTableRowBase` structs contain authored data only.
2. An adapter calls `FindRow<T>` inside local scope, reports missing/wrong row
   types as validation errors, and immediately copies values into plain-C++
   definitions owned by the battle-content snapshot.
3. The core never stores a `UDataTable*`, row pointer, `UObject*`, soft-object
   loader, or `FDataTableRowHandle` in battle state.
4. Validate identifiers, ranges, references, duplicate IDs, target classes,
   modifier bounds, and unsupported flags while constructing the snapshot.
5. Sort copied definitions by stable typed ID. Never let `TMap`/row iteration
   order choose trigger, effect, or RNG order.
6. A reimport may build a new content snapshot between battles. An active
   battle retains its immutable copied snapshot and does not change mid-battle.

Relevant official pages are [Data Driven Gameplay Elements](https://dev.epicgames.com/documentation/unreal-engine/data-driven-gameplay-elements-in-unreal-engine),
[`UDataTable::FindRow`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UDataTable/FindRow?lang=en-US),
and [`FTableRowBase`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FTableRowBase?lang=en-US).

## Resolved source question and acceptance

### Q-B00B-01 — resolved 2026-08-20

The user authorized the narrow supplementary Gen IX source set recorded above.
The review established setup/cap facts, the shared pre-move placement of special
commands, Revive's half-HP effect, Full Heal's cure set, and X Attack's two-stage
effect. It did not establish the exact modern obedience algorithm, relative
special-command order, odd-HP Revive rounding, or X Attack use at `+6`.

The review stopped without editing and presented those gaps. The user then
approved these explicit Solarus closures:

- deterministic above-cap refusal before PP and RNG;
- `Run > voluntary switch > Bag/capture > moves`;
- Revive restores `max(1, floor(MaxHP / 2))`;
- X Attack is rejected at `+6` without consuming its item or action.

The Full Heal source rule and all execution-time resource checkpoints are frozen
above. `Q-B00B-01` blocks no package. No other unresolved P0 formula, type,
stage, damage, ordering, RNG-interface, or Data Table boundary remains in B00B.

## Coverage audit

| B00B required area | State in this file |
|---|---|
| permanent stats, validation, natures, rounding | frozen |
| stages and accuracy/evasion `-6..+6` | frozen |
| 18x18 type chart and dual-type order | frozen |
| PP, accuracy, Struggle, criticals, damage order | frozen |
| RNG eligibility and call order | frozen, including deterministic obedience and Bag execution checkpoints |
| action classes, ties, and Quick Claw | frozen |
| standard obedience | frozen as sourced setup facts plus accepted Solarus deterministic outcome |
| major status and approved volatiles | frozen |
| weather, terrain, hazards, screens, rooms, side conditions | frozen |
| approved Abilities and held items | frozen |
| Hyper Potion and Poke Ball | frozen |
| Revive, Full Heal, X Attack | frozen |
| configured wild flee and Solarus Run | frozen |
| SV capture indicator, critical, and shake math | frozen for approved neutral Poke Ball proof; unidentified special mode rejected |
| action/faint/replacement/end-turn/outcome order | frozen |
| UE 5.8 Data Table boundary | frozen |

## B00B completion point

B00B is accepted. Do not start C01 in this session. Record this file's SHA-256
in B00 and the roadmap, verify that source/configuration stayed unchanged, and
stop. The exact next package for a later session is C01A.
