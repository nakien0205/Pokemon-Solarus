# Pokemon GIF to Paper 2D Importer

This editor-only tool turns one animated GIF into:

- one transparent PNG texture atlas under `Game/SourceAssets/Pokemon/`;
- deduplicated PaperSprite frame assets;
- one PaperFlipbook that preserves the GIF timing;
- bottom-center pivots, no collision, nearest filtering, and the built-in
  `MaskedUnlitSpriteMaterial`.

Name inputs `<pokemon>_front.gif` or `<pokemon>_back.gif`. In Unreal Editor's
Python console, run:

```python
import pokemon_sprite_importer as psi
psi.import_pokemon_gif(r"C:\path\charizard_back.gif")
```

To import all GIFs directly inside one folder, run:

```python
import pokemon_sprite_importer as psi
psi.import_pokemon_gif_folder(r"C:\path\pokemon-gifs")
```

Folder import is non-recursive: nested folders and non-GIF files are ignored.
Before importing anything, every direct GIF is checked for the required
`_front.gif` or `_back.gif` suffix. A source folder named `default`, `female`,
`shiny`, or `shiny-female` selects that appearance automatically. An
unrecognized folder name keeps the default behavior for backward
compatibility. You can also pass `appearance="female"`, `appearance="shiny"`,
or `appearance="shiny-female"` explicitly when importing one GIF or folder.

Pokemon, appearance, and view names are capitalized in generated paths. The
default appearance deliberately keeps the original layout and asset names:

```text
/Game/Art/Pokemon/Bulbasaur/
  Front/
    FB_Bulbasaur_Front_Idle
    T_Bulbasaur_Front_Idle_Sheet
    Frames/
  Back/
    FB_Bulbasaur_Back_Idle
    T_Bulbasaur_Back_Idle_Sheet
    Frames/
```

The other appearances are separate and include their appearance in every
generated asset name:

```text
/Game/Art/Pokemon/Bulbasaur/
  Female/Front/Frames/
  Female/Back/Frames/
  Shiny/Front/Frames/
  Shiny/Back/Frames/
  ShinyFemale/Front/Frames/
  ShinyFemale/Back/Frames/
```

For example, the shiny front assets are
`T_Bulbasaur_Shiny_Front_Idle_Sheet`, `FB_Bulbasaur_Shiny_Front_Idle`, and
`SPR_Bulbasaur_Shiny_Front_Idle_000`. Generated PNG/JSON source files mirror
the same hierarchy under `Game/SourceAssets/Pokemon/`.

Use `replace_existing=True` only when intentionally updating previously
generated assets. The default is safe and refuses to overwrite them.

The converter needs the normal Windows Python installation and Pillow:

```powershell
py -3 -m pip install -r "Game\Tools\PokemonSpriteImporter\requirements.txt"
```

## Download all Generation V animated GIFs

Run the standalone downloader from the repository root in PowerShell or CMD:

```powershell
py -3 "Game\Tools\PokemonSpriteImporter\download_generation_v_gifs.py"
```

The downloader pins one current commit of the public PokeAPI sprites
repository, resolves Pokemon and alternate-form names through PokeAPI, and
writes verified GIFs under:

```text
Game/Content/Art/GIF/generation-v/
  default/
  shiny/
  female/
  shiny-female/
```

Every filename still ends in `_front.gif` or `_back.gif`. Examples include
`bulbasaur_back.gif`, `deoxys-attack_front.gif`, and `unown-a_front.gif`.
Exact repository aliases are stored once and every original path remains in
`download_manifest.csv`. `pokemon_name_map.csv` records each source identifier
and resolved name, while `download_summary.json` records the pinned commit and
verification totals.

Re-running the command verifies and reuses matching files. It refuses to
replace a mismatched file unless `--overwrite` is provided. Use `--dry-run` to
validate the live catalogs without writing or downloading anything.

Import each downloaded appearance folder from Unreal Editor's Python console:

```python
import importlib
import pokemon_sprite_importer as psi
importlib.reload(psi)

root = r"D:\Python\Projects\Pokemon Solarus\Game\Content\Art\GIF\generation-v"
psi.import_pokemon_gif_folder(root + r"\default")
psi.import_pokemon_gif_folder(root + r"\female")
psi.import_pokemon_gif_folder(root + r"\shiny")
psi.import_pokemon_gif_folder(root + r"\shiny-female")
```

The `importlib.reload` line ensures an Unreal Editor session that previously
loaded the module uses the updated variant-aware code. Existing default
`Front` and `Back` assets do not need to be reimported.

Source and licensing details are preserved in `download_summary.json`; see the
[PokeAPI sprites repository](https://github.com/PokeAPI/sprites).
