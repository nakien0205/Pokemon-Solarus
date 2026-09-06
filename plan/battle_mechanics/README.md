# Battle Mechanics Plan Routing

This directory contains the detailed Battle-system roadmap packages.

Do **not** read the packages sequentially and do not load the entire Battle plan set. Identify the package that owns the requested behavior and read only that package plus direct dependencies when necessary.

## Package routing

| Package | Owns                                          |
| ------- | --------------------------------------------- |
| `01-*`  | Live baseline and rules snapshot              |
| `02-*`  | Core contracts, events, and RNG               |
| `03-*`  | Stats, types, moves, and data adapters        |
| `04-*`  | Battle state, snapshots, and decisions        |
| `05-*`  | Actions, ordering, and targeting              |
| `06-*`  | Hit, damage, effects, and outcomes            |
| `07-*`  | Parties, switching, and replacements          |
| `08-*`  | Status, volatiles, field, and side conditions |
| `09-*`  | Abilities, held items, and Battle items       |
| `10-*`  | Encounters, capture, escape, and partner flow |
| `11-*`  | Canonical proof content                       |
| `12-*`  | Integration and release gate                  |

## Which index should I use?

```text
README.md
    = quick package routing

00-roadmap-index.md
    = overall roadmap and status

01–12 package files
    = detailed requirements / implementation packages

reference/modern-rules-snapshot.md
    = frozen modern Pokémon rules reference when exact rule lookup is needed
```

Use `00-roadmap-index.md` when roadmap status or package sequencing is the question.

Use the specific `01–12` package when implementing or validating behavior owned by that package.

Use `reference/modern-rules-snapshot.md` only when the task needs the exact frozen rules reference; do not load it as routine context.

## Agent routing rule

> Identify the owning package first. Read that package, then only the direct prerequisite or reference material required to resolve the task.

Do not treat this directory as one large specification that must be loaded before Battle work.
