# Unreal Engine 5.8.1 — Pokemon Solarus Input Contract

**Last verified:** 2026-08-19

## Platform and Device Scope

- Target: Windows PC only.
- Supported gameplay input: keyboard and compatible Xbox, PlayStation, and
  Nintendo controllers connected to the PC.
- This does not authorize PlayStation, Xbox, or Nintendo console releases.
- Mouse and touch gameplay are not supported.

## Native Mapping

| Action | Keyboard | Xbox | PlayStation | Nintendo |
|---|---|---|---|---|
| Move/navigation | Arrow keys | D-pad/left stick | D-pad/left stick | D-pad/left stick |
| Confirm/interact | C | A | Cross | A |
| Cancel/back/menu | X | B | Circle | B |
| Sprint | Z | X | Square | Y |
| Battle Info | V | Menu | Options | Plus |
| Registered Items | D | Menu | Options | Plus |

These are native-label mappings. Nintendo Confirm and Cancel therefore occupy
different physical face-button positions from their Xbox and PlayStation
counterparts.

## Resolution Rules

- Keyboard/D-pad conflicts: the most recently pressed direction wins.
- Quantize analog-stick movement to one cardinal direction. Do not allow
  diagonals. Apply a suitable dead zone before quantization so stick noise does
  not count as intentional input.
- In Auto sprint mode, the Sprint action does nothing.
- Cancel goes back while an interaction or menu is active; otherwise it opens
  the field menu.

## Prompt Rules

- Prompts follow the most recently used supported input device, not controller
  connection state.
- Only meaningful keyboard or gamepad activity may switch prompts. Ignore mouse,
  touch, and analog-stick drift for this purpose.
- Recognized controllers display matching native glyphs.
- Unknown controllers display generic gamepad prompts.

Epic documents `Get Current Input Type` as returning the type based on the last
input received. Epic's controller data also supports specific gamepads on PC
with a Generic gamepad fallback. Those facilities validate the behavior, but
the implementation must remain lean; full CommonUI architecture requires a
separate decision.

## Current and Future Scope

The current two-Pokémon battle prototype implements only:

- navigation;
- Confirm;
- Cancel;
- Battle Info on `V`.

Overworld movement, Sprint, Bag, the main menu, and Registered Items are future
scope. Automatic registration of Key Items belongs in a future Bag/Registered
Items design document, not in technical preferences.

## Official Sources

- [Enhanced Input](https://dev.epicgames.com/documentation/unreal-engine/enhanced-input-in-unreal-engine)
- [Get Current Input Type](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/CommonInputSubsystem/GetCurrentInputType)
- [Common UI controller data and platform-specific glyphs](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-quickstart-guide-for-unreal-engine)
- [UE 5.8 unified input notes](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes)

