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
`_front.gif` or `_back.gif` suffix. Pokemon and view names are capitalized in
the generated paths. For example, `bulbasaur_front.gif` creates Unreal assets
under `/Game/Art/Pokemon/Bulbasaur/Front`, which maps to
`Game/Content/Art/Pokemon/Bulbasaur/Front/` on disk. Sprite frames are saved in
its `Frames/` subfolder. Generated PNG/JSON source files remain under
`Game/SourceAssets/Pokemon/`.

Use `replace_existing=True` only when intentionally updating previously
generated assets. The default is safe and refuses to overwrite them.

The converter needs the normal Windows Python installation and Pillow:

```powershell
py -3 -m pip install -r "Game\Tools\PokemonSpriteImporter\requirements.txt"
```
