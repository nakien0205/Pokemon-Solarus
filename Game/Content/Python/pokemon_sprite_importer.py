"""Reusable UE 5.8 Paper 2D importer for front/back Pokemon GIFs.

Run this module inside Unreal Editor's Python environment. The public functions
accept either one GIF path or one folder of GIFs and create texture atlases,
PaperSprite frame assets, and timing-correct PaperFlipbooks.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any, Iterable

import unreal


DEFAULT_DESTINATION_ROOT = "/Game/Art/Pokemon"
DEFAULT_PIXELS_PER_UNREAL_UNIT = 0.5
CONVERTER_RELATIVE_PATH = Path("Tools/PokemonSpriteImporter/convert_pokemon_gif.py")
SOURCE_ASSET_RELATIVE_ROOT = Path("SourceAssets/Pokemon")
MASKED_UNLIT_MATERIAL_PATH = (
    "/Paper2D/MaskedUnlitSpriteMaterial.MaskedUnlitSpriteMaterial"
)
VALID_GIF_FILENAME_PATTERN = re.compile(
    r"(?P<pokemon>.+)_(?P<view>front|back)\.gif",
    re.IGNORECASE,
)


class PokemonSpriteImportError(RuntimeError):
    """Raised when conversion or Unreal asset creation fails."""


def _project_directory() -> Path:
    return Path(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    ).resolve()


def _find_external_python() -> list[str]:
    launcher = shutil.which("py")
    if launcher:
        return [launcher, "-3"]
    python = shutil.which("python")
    if python:
        return [python]
    raise PokemonSpriteImportError(
        "No normal Python installation was found. Install Python 3 with Pillow, "
        "then restart Unreal Editor."
    )


def _run_converter(
    gif_path: Path,
    *,
    project_directory: Path,
    source_output_root: Path,
    overwrite: bool,
) -> tuple[Path, dict[str, Any]]:
    converter = project_directory / CONVERTER_RELATIVE_PATH
    if not converter.is_file():
        raise PokemonSpriteImportError(f"GIF converter is missing: {converter}")

    command = [
        *_find_external_python(),
        str(converter),
        str(gif_path),
        "--output-root",
        str(source_output_root),
    ]
    if overwrite:
        command.append("--overwrite")

    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise PokemonSpriteImportError(
            "GIF conversion failed. Confirm Pillow is installed with "
            f"'py -3 -m pip install Pillow'.\n{detail}"
        )

    try:
        conversion = json.loads(completed.stdout.strip().splitlines()[-1])
        manifest_path = Path(conversion["manifest_path"])
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (IndexError, KeyError, OSError, json.JSONDecodeError) as exc:
        raise PokemonSpriteImportError(
            "The GIF converter did not return a readable manifest."
        ) from exc
    return Path(conversion["atlas_path"]), manifest


def _asset_path(package_path: str, asset_name: str) -> str:
    return f"{package_path.rstrip('/')}/{asset_name}"


def _preflight_generated_assets(
    *,
    destination_path: str,
    frames_path: str,
    manifest: dict[str, Any],
    replace_existing: bool,
) -> None:
    if replace_existing:
        return

    expected_paths = [
        _asset_path(destination_path, manifest["texture_name"]),
        _asset_path(destination_path, manifest["flipbook_name"]),
    ]
    expected_paths.extend(
        _asset_path(
            frames_path,
            f"{manifest['sprite_name_prefix']}_{sprite_index:03d}",
        )
        for sprite_index in range(int(manifest["unique_sprite_count"]))
    )
    existing_paths = [
        path
        for path in expected_paths
        if unreal.EditorAssetLibrary.does_asset_exist(path)
    ]
    if existing_paths:
        preview = "\n".join(existing_paths[:5])
        remaining = len(existing_paths) - 5
        if remaining > 0:
            preview += f"\n...and {remaining} more"
        raise PokemonSpriteImportError(
            "Generated assets already exist. Nothing was imported. "
            "Pass replace_existing=True only when you intend to update them:\n"
            + preview
        )


def _load_typed_asset(asset_path: str, expected_type: type) -> Any:
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise PokemonSpriteImportError(f"Failed to load asset: {asset_path}")
    if not isinstance(asset, expected_type):
        raise PokemonSpriteImportError(
            f"Existing asset has the wrong type: {asset_path} "
            f"({type(asset).__name__}, expected {expected_type.__name__})"
        )
    return asset


def _create_or_load_asset(
    asset_name: str,
    package_path: str,
    asset_class: type,
    factory: Any,
    *,
    replace_existing: bool,
) -> Any:
    full_asset_path = _asset_path(package_path, asset_name)
    if unreal.EditorAssetLibrary.does_asset_exist(full_asset_path):
        if not replace_existing:
            raise PokemonSpriteImportError(
                f"Asset already exists: {full_asset_path}. "
                "Pass replace_existing=True only when you intend to update it."
            )
        return _load_typed_asset(full_asset_path, asset_class)

    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name,
        package_path,
        asset_class,
        factory,
    )
    if asset is None:
        raise PokemonSpriteImportError(f"Failed to create asset: {full_asset_path}")
    return asset


def _import_texture(
    atlas_path: Path,
    destination_path: str,
    texture_name: str,
    *,
    replace_existing: bool,
) -> unreal.Texture2D:
    texture_asset_path = _asset_path(destination_path, texture_name)
    if (
        unreal.EditorAssetLibrary.does_asset_exist(texture_asset_path)
        and not replace_existing
    ):
        raise PokemonSpriteImportError(
            f"Asset already exists: {texture_asset_path}. "
            "Pass replace_existing=True only when you intend to update it."
        )

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(atlas_path))
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", texture_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", replace_existing)
    task.set_editor_property("replace_existing_settings", False)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported_objects = list(task.get_objects())
    texture = imported_objects[0] if imported_objects else None
    if texture is None:
        texture = unreal.EditorAssetLibrary.load_asset(texture_asset_path)
    if not isinstance(texture, unreal.Texture2D):
        raise PokemonSpriteImportError(
            f"PNG import did not create the expected Texture2D: {texture_asset_path}"
        )

    texture.set_editor_property(
        "mip_gen_settings",
        unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
    )
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property(
        "compression_settings",
        unreal.TextureCompressionSettings.TC_EDITOR_ICON,
    )
    texture.set_editor_property(
        "lod_group",
        unreal.TextureGroup.TEXTUREGROUP_PIXELS2D,
    )
    return texture


def _configure_sprite(
    sprite: unreal.PaperSprite,
    *,
    texture: unreal.Texture2D,
    source_x: int,
    source_y: int,
    frame_width: int,
    frame_height: int,
    atlas_width: int,
    atlas_height: int,
    pixels_per_unreal_unit: float,
    material: unreal.MaterialInterface,
) -> None:
    sprite.set_editor_property("source_texture", texture)
    sprite.set_editor_property(
        "source_uv",
        unreal.Vector2D(float(source_x), float(source_y)),
    )
    sprite.set_editor_property(
        "source_dimension",
        unreal.Vector2D(float(frame_width), float(frame_height)),
    )
    sprite.set_editor_property(
        "source_texture_dimension",
        unreal.Vector2D(float(atlas_width), float(atlas_height)),
    )
    sprite.set_editor_property("pixels_per_unreal_unit", pixels_per_unreal_unit)
    sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.BOTTOM_CENTER)
    sprite.set_editor_property(
        "sprite_collision_domain",
        unreal.SpriteCollisionMode.NONE,
    )
    sprite.set_editor_property("default_material", material)


def _save_assets(assets: Iterable[Any]) -> None:
    failed: list[str] = []
    for asset in assets:
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            asset,
            only_if_is_dirty=False,
        ):
            failed.append(asset.get_path_name())
    if failed:
        raise PokemonSpriteImportError(
            "Unreal could not save these assets:\n" + "\n".join(failed)
        )


def import_pokemon_gif(
    gif_path: str,
    *,
    destination_root: str = DEFAULT_DESTINATION_ROOT,
    pixels_per_unreal_unit: float = DEFAULT_PIXELS_PER_UNREAL_UNIT,
    replace_existing: bool = False,
    project_directory_override: str | None = None,
    source_output_root_override: str | None = None,
) -> dict[str, Any]:
    """Create a timing-correct Paper 2D Flipbook from one front/back GIF.

    The filename must end in ``_front.gif`` or ``_back.gif``. Generated source
    PNG/JSON files live outside ``Content``; Unreal assets are created under
    ``destination_root/<Pokemon>/<Front|Back>``.
    """

    source_gif = Path(gif_path).expanduser().resolve()
    if not source_gif.is_file():
        raise PokemonSpriteImportError(f"GIF does not exist: {source_gif}")
    if pixels_per_unreal_unit <= 0:
        raise PokemonSpriteImportError("pixels_per_unreal_unit must be greater than zero.")
    if not destination_root.startswith("/Game/"):
        raise PokemonSpriteImportError("destination_root must start with '/Game/'.")

    project_directory = (
        Path(project_directory_override).resolve()
        if project_directory_override
        else _project_directory()
    )
    source_output_root = (
        Path(source_output_root_override).resolve()
        if source_output_root_override
        else project_directory / SOURCE_ASSET_RELATIVE_ROOT
    )
    atlas_path, manifest = _run_converter(
        source_gif,
        project_directory=project_directory,
        source_output_root=source_output_root,
        overwrite=replace_existing,
    )

    pokemon = manifest["pokemon"]
    view = manifest["view"]
    destination_path = f"{destination_root.rstrip('/')}/{pokemon}/{view}"
    frames_path = f"{destination_path}/Frames"
    _preflight_generated_assets(
        destination_path=destination_path,
        frames_path=frames_path,
        manifest=manifest,
        replace_existing=replace_existing,
    )
    unreal.EditorAssetLibrary.make_directory(destination_path)
    unreal.EditorAssetLibrary.make_directory(frames_path)

    material = unreal.load_asset(MASKED_UNLIT_MATERIAL_PATH)
    if not isinstance(material, unreal.MaterialInterface):
        raise PokemonSpriteImportError(
            f"Required Paper 2D material was not found: {MASKED_UNLIT_MATERIAL_PATH}"
        )

    texture = _import_texture(
        atlas_path,
        destination_path,
        manifest["texture_name"],
        replace_existing=replace_existing,
    )

    sprites: list[unreal.PaperSprite] = []
    frame_width = int(manifest["frame_width"])
    frame_height = int(manifest["frame_height"])
    columns = int(manifest["atlas_columns"])
    sprite_count = int(manifest["unique_sprite_count"])
    for sprite_index in range(sprite_count):
        sprite_name = f"{manifest['sprite_name_prefix']}_{sprite_index:03d}"
        sprite = _create_or_load_asset(
            sprite_name,
            frames_path,
            unreal.PaperSprite,
            unreal.PaperSpriteFactory(),
            replace_existing=replace_existing,
        )
        column = sprite_index % columns
        row = sprite_index // columns
        _configure_sprite(
            sprite,
            texture=texture,
            source_x=column * frame_width,
            source_y=row * frame_height,
            frame_width=frame_width,
            frame_height=frame_height,
            atlas_width=int(manifest["atlas_width"]),
            atlas_height=int(manifest["atlas_height"]),
            pixels_per_unreal_unit=pixels_per_unreal_unit,
            material=material,
        )
        sprites.append(sprite)

    flipbook = _create_or_load_asset(
        manifest["flipbook_name"],
        destination_path,
        unreal.PaperFlipbook,
        unreal.PaperFlipbookFactory(),
        replace_existing=replace_existing,
    )
    key_frames: list[unreal.PaperFlipbookKeyFrame] = []
    for key_frame_data in manifest["key_frames"]:
        key_frame = unreal.PaperFlipbookKeyFrame()
        key_frame.set_editor_property(
            "sprite",
            sprites[int(key_frame_data["sprite_index"])],
        )
        key_frame.set_editor_property("frame_run", int(key_frame_data["frame_run"]))
        key_frames.append(key_frame)

    flipbook.set_editor_property("frames_per_second", manifest["frames_per_second"])
    flipbook.set_editor_property("key_frames", key_frames)
    flipbook.set_editor_property("default_material", material)
    flipbook.set_editor_property(
        "collision_source",
        unreal.FlipbookCollisionMode.NO_COLLISION,
    )
    _save_assets([texture, *sprites, flipbook])

    result = {
        "pokemon": pokemon,
        "view": view,
        "texture": texture.get_path_name(),
        "flipbook": flipbook.get_path_name(),
        "sprite_count": len(sprites),
        "key_frame_count": len(key_frames),
        "source_frame_count": int(manifest["source_frame_count"]),
        "frames_per_second": float(manifest["frames_per_second"]),
        "duration_seconds": float(manifest["duration_ms"]) / 1000.0,
        "atlas_path": str(atlas_path),
    }
    unreal.log("Pokemon GIF import complete: " + json.dumps(result, sort_keys=True))
    return result


def import_pokemon_gifs(
    gif_paths: Iterable[str],
    **kwargs: Any,
) -> list[dict[str, Any]]:
    """Import several front/back GIFs with the same settings."""

    return [import_pokemon_gif(path, **kwargs) for path in gif_paths]


def _discover_pokemon_gifs(folder_path: str | Path) -> list[Path]:
    source_folder = Path(folder_path).expanduser().resolve()
    if not source_folder.is_dir():
        raise PokemonSpriteImportError(
            f"Folder does not exist or is not a directory: {source_folder}"
        )

    try:
        gif_paths = sorted(
            (
                path.resolve()
                for path in source_folder.iterdir()
                if path.is_file() and path.suffix.lower() == ".gif"
            ),
            key=lambda path: path.name.casefold(),
        )
    except OSError as exc:
        raise PokemonSpriteImportError(
            f"Could not read GIF folder: {source_folder}"
        ) from exc

    if not gif_paths:
        raise PokemonSpriteImportError(
            f"Folder does not contain any GIF files: {source_folder}"
        )

    invalid_names: list[str] = []
    for gif_path in gif_paths:
        match = VALID_GIF_FILENAME_PATTERN.fullmatch(gif_path.name)
        if match is None or re.search(r"[A-Za-z0-9]", match["pokemon"]) is None:
            invalid_names.append(gif_path.name)

    if invalid_names:
        raise PokemonSpriteImportError(
            "These GIF filenames are invalid. Every GIF must be named "
            "'<pokemon>_front.gif' or '<pokemon>_back.gif': "
            + ", ".join(invalid_names)
        )

    return gif_paths


def import_pokemon_gif_folder(
    folder_path: str,
    *,
    destination_root: str = DEFAULT_DESTINATION_ROOT,
    pixels_per_unreal_unit: float = DEFAULT_PIXELS_PER_UNREAL_UNIT,
    replace_existing: bool = False,
    project_directory_override: str | None = None,
    source_output_root_override: str | None = None,
) -> list[dict[str, Any]]:
    """Import every valid GIF directly inside one folder.

    Subfolders and non-GIF files are ignored. Every direct GIF filename is
    validated before any import starts, and must end in ``_front.gif`` or
    ``_back.gif``.
    """

    gif_paths = _discover_pokemon_gifs(folder_path)
    return import_pokemon_gifs(
        [str(path) for path in gif_paths],
        destination_root=destination_root,
        pixels_per_unreal_unit=pixels_per_unreal_unit,
        replace_existing=replace_existing,
        project_directory_override=project_directory_override,
        source_output_root_override=source_output_root_override,
    )
