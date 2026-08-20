"""Convert one animated Pokemon GIF into an Unreal-friendly texture atlas.

This module runs in the normal system Python because Unreal's embedded Python
does not include Pillow. The companion ``pokemon_sprite_importer`` module calls
it automatically from inside Unreal Editor.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from dataclasses import dataclass
from functools import reduce
from math import gcd
from pathlib import Path
from typing import Any

from PIL import Image


MANIFEST_VERSION = 1
DEFAULT_FRAME_DURATION_MS = 100
MAX_TEXTURE_SIZE = 8192
VALID_VIEWS = {"front": "Front", "back": "Back"}
VALID_APPEARANCES = {
    "default": "Default",
    "female": "Female",
    "shiny": "Shiny",
    "shiny-female": "ShinyFemale",
}
KNOWN_NAME_CORRECTIONS = {
    # The current downloaded files use this common misspelling.
    "venasaur": "Venusaur",
}


class GifConversionError(RuntimeError):
    """Raised when a GIF cannot be converted safely."""


@dataclass(frozen=True)
class GifIdentity:
    pokemon: str
    view: str


@dataclass(frozen=True)
class ConversionResult:
    atlas_path: Path
    manifest_path: Path
    manifest: dict[str, Any]
    reused_existing_output: bool


def normalize_appearance(appearance: str) -> str:
    """Return the canonical folder/name label for a supported appearance."""

    appearance_key = appearance.strip().casefold()
    if appearance_key not in VALID_APPEARANCES:
        choices = ", ".join(VALID_APPEARANCES)
        raise GifConversionError(f"appearance must be one of: {choices}.")
    return VALID_APPEARANCES[appearance_key]


def _pascal_case(value: str) -> str:
    parts = [part for part in re.split(r"[^A-Za-z0-9]+", value) if part]
    if not parts:
        raise GifConversionError("The GIF filename does not contain a Pokemon name.")
    return "".join(part[:1].upper() + part[1:].lower() for part in parts)


def parse_gif_identity(
    gif_path: str | Path,
    *,
    pokemon_name: str | None = None,
    view: str | None = None,
) -> GifIdentity:
    """Read ``Pokemon`` and ``Front``/``Back`` from a GIF filename.

    The expected filename is ``<pokemon>_front.gif`` or
    ``<pokemon>_back.gif``. Explicit overrides are available for unusual names.
    """

    path = Path(gif_path)
    if path.suffix.lower() != ".gif":
        raise GifConversionError(f"Expected a .gif file, received: {path.name}")

    match = re.fullmatch(r"(.+?)_(front|back)", path.stem, re.IGNORECASE)
    inferred_name = match.group(1) if match else None
    inferred_view = match.group(2) if match else None

    if pokemon_name is None and inferred_name is None:
        raise GifConversionError(
            "Name the file '<pokemon>_front.gif' or '<pokemon>_back.gif', "
            "or provide pokemon_name and view explicitly."
        )
    if view is None and inferred_view is None:
        raise GifConversionError(
            "The GIF filename must end in '_front' or '_back', or view must be provided."
        )

    raw_name = pokemon_name or inferred_name or ""
    normalized_key = re.sub(r"[^a-z0-9]", "", raw_name.lower())
    normalized_name = KNOWN_NAME_CORRECTIONS.get(normalized_key, _pascal_case(raw_name))

    normalized_view_key = (view or inferred_view or "").lower()
    if normalized_view_key not in VALID_VIEWS:
        raise GifConversionError("view must be either 'front' or 'back'.")

    return GifIdentity(
        pokemon=normalized_name,
        view=VALID_VIEWS[normalized_view_key],
    )


def _canonical_rgba(frame: Image.Image) -> Image.Image:
    """Remove invisible RGB noise without changing visible pixels."""

    rgba = frame.convert("RGBA")
    canonical = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    canonical.paste(rgba, (0, 0), rgba.getchannel("A"))
    return canonical


def _choose_grid(
    frame_count: int,
    frame_width: int,
    frame_height: int,
    max_texture_size: int,
) -> tuple[int, int]:
    """Choose a compact, near-square grid that stays within UE's texture limit."""

    best: tuple[float, int, int] | None = None
    max_columns = min(frame_count, max_texture_size // frame_width)
    for columns in range(1, max_columns + 1):
        rows = math.ceil(frame_count / columns)
        atlas_width = columns * frame_width
        atlas_height = rows * frame_height
        if atlas_height > max_texture_size:
            continue

        empty_cells = (columns * rows) - frame_count
        aspect_penalty = abs(math.log(atlas_width / atlas_height))
        waste_penalty = empty_cells / frame_count
        score = aspect_penalty + (waste_penalty * 0.25)
        candidate = (score, columns, rows)
        if best is None or candidate < best:
            best = candidate

    if best is None:
        raise GifConversionError(
            f"The animation cannot fit inside a {max_texture_size}x{max_texture_size} atlas."
        )
    return best[1], best[2]


def _source_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_existing_result(
    manifest_path: Path,
    atlas_path: Path,
    source_sha256: str,
) -> ConversionResult | None:
    if not manifest_path.is_file() or not atlas_path.is_file():
        return None
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if (
        manifest.get("manifest_version") == MANIFEST_VERSION
        and manifest.get("source_sha256") == source_sha256
    ):
        # Version 1 default manifests created before appearance support remain
        # valid and keep their original paths and asset names.
        manifest.setdefault("appearance", "Default")
        return ConversionResult(
            atlas_path=atlas_path,
            manifest_path=manifest_path,
            manifest=manifest,
            reused_existing_output=True,
        )
    return None


def convert_pokemon_gif(
    gif_path: str | Path,
    output_root: str | Path,
    *,
    pokemon_name: str | None = None,
    view: str | None = None,
    appearance: str = "default",
    overwrite: bool = False,
    max_texture_size: int = MAX_TEXTURE_SIZE,
) -> ConversionResult:
    """Convert one front/back GIF to an atlas and timing manifest.

    Identical frames share one sprite cell. Consecutive duplicate frames are
    merged into one Flipbook key frame with a longer ``frame_run``.
    """

    source_path = Path(gif_path).expanduser().resolve()
    if not source_path.is_file():
        raise GifConversionError(f"GIF does not exist: {source_path}")

    identity = parse_gif_identity(
        source_path,
        pokemon_name=pokemon_name,
        view=view,
    )
    appearance_label = normalize_appearance(appearance)
    output_directory = Path(output_root).expanduser().resolve() / identity.pokemon
    if appearance_label != "Default":
        output_directory /= appearance_label
    output_directory /= identity.view

    name_stem = f"{identity.pokemon}_{identity.view}"
    if appearance_label != "Default":
        name_stem = f"{identity.pokemon}_{appearance_label}_{identity.view}"
    texture_name = f"T_{name_stem}_Idle_Sheet"
    flipbook_name = f"FB_{name_stem}_Idle"
    sprite_name_prefix = f"SPR_{name_stem}_Idle"
    atlas_path = output_directory / f"{texture_name}.png"
    manifest_path = output_directory / f"{texture_name}.json"
    source_sha256 = _source_sha256(source_path)

    existing = _read_existing_result(manifest_path, atlas_path, source_sha256)
    if existing is not None:
        return existing
    if not overwrite and (atlas_path.exists() or manifest_path.exists()):
        raise GifConversionError(
            f"Generated output already exists for different GIF data: {output_directory}. "
            "Use overwrite=True only when you intend to update it."
        )

    unique_frames: list[Image.Image] = []
    unique_frame_bytes: list[bytes] = []
    frame_index_by_hash: dict[str, list[int]] = {}
    source_frames: list[tuple[int, int]] = []
    durations_ms: list[int] = []
    alpha_values: set[int] = set()

    try:
        with Image.open(source_path) as gif:
            if gif.format != "GIF":
                raise GifConversionError(f"Pillow did not recognize this as a GIF: {source_path}")
            if getattr(gif, "n_frames", 1) < 2:
                raise GifConversionError("The GIF contains only one frame; use a static Sprite instead.")

            frame_width, frame_height = gif.size
            source_frame_count = gif.n_frames
            loop_count = int(gif.info.get("loop", 0))

            for frame_number in range(source_frame_count):
                gif.seek(frame_number)
                frame = _canonical_rgba(gif.copy())
                duration_ms = int(gif.info.get("duration", DEFAULT_FRAME_DURATION_MS) or 0)
                if duration_ms <= 0:
                    duration_ms = DEFAULT_FRAME_DURATION_MS

                frame_bytes = frame.tobytes()
                frame_hash = hashlib.sha256(frame_bytes).hexdigest()
                sprite_index = None
                for candidate_index in frame_index_by_hash.get(frame_hash, []):
                    if unique_frame_bytes[candidate_index] == frame_bytes:
                        sprite_index = candidate_index
                        break
                if sprite_index is None:
                    sprite_index = len(unique_frames)
                    unique_frames.append(frame)
                    unique_frame_bytes.append(frame_bytes)
                    frame_index_by_hash.setdefault(frame_hash, []).append(sprite_index)

                alpha_values.update(frame.getchannel("A").getextrema())
                source_frames.append((sprite_index, duration_ms))
                durations_ms.append(duration_ms)
    except OSError as exc:
        raise GifConversionError(f"Failed to decode GIF: {source_path}") from exc

    base_frame_duration_ms = reduce(gcd, durations_ms)
    if base_frame_duration_ms <= 0:
        base_frame_duration_ms = DEFAULT_FRAME_DURATION_MS
    frames_per_second = 1000.0 / base_frame_duration_ms

    key_frames: list[dict[str, int]] = []
    for sprite_index, duration_ms in source_frames:
        frame_run = max(1, int(round(duration_ms / base_frame_duration_ms)))
        if key_frames and key_frames[-1]["sprite_index"] == sprite_index:
            key_frames[-1]["frame_run"] += frame_run
        else:
            key_frames.append(
                {
                    "sprite_index": sprite_index,
                    "frame_run": frame_run,
                }
            )

    columns, rows = _choose_grid(
        len(unique_frames),
        frame_width,
        frame_height,
        max_texture_size,
    )
    atlas_width = columns * frame_width
    atlas_height = rows * frame_height
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    for sprite_index, frame in enumerate(unique_frames):
        x = (sprite_index % columns) * frame_width
        y = (sprite_index // columns) * frame_height
        atlas.paste(frame, (x, y))

    manifest: dict[str, Any] = {
        "manifest_version": MANIFEST_VERSION,
        "source_file": source_path.name,
        "source_sha256": source_sha256,
        "pokemon": identity.pokemon,
        "view": identity.view,
        "appearance": appearance_label,
        "texture_name": texture_name,
        "flipbook_name": flipbook_name,
        "sprite_name_prefix": sprite_name_prefix,
        "frame_width": frame_width,
        "frame_height": frame_height,
        "source_frame_count": source_frame_count,
        "unique_sprite_count": len(unique_frames),
        "key_frame_count": len(key_frames),
        "base_frame_duration_ms": base_frame_duration_ms,
        "frames_per_second": frames_per_second,
        "duration_ms": sum(durations_ms),
        "loop_count": loop_count,
        "atlas_columns": columns,
        "atlas_rows": rows,
        "atlas_width": atlas_width,
        "atlas_height": atlas_height,
        "has_transparency": min(alpha_values, default=255) < 255,
        "key_frames": key_frames,
    }

    output_directory.mkdir(parents=True, exist_ok=True)
    atlas.save(atlas_path, format="PNG", compress_level=9)
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    return ConversionResult(
        atlas_path=atlas_path,
        manifest_path=manifest_path,
        manifest=manifest,
        reused_existing_output=False,
    )


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert a <pokemon>_front.gif or <pokemon>_back.gif for UE Paper 2D."
    )
    parser.add_argument("gif_path", type=Path)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--pokemon-name")
    parser.add_argument("--view", choices=("front", "back"))
    parser.add_argument(
        "--appearance",
        choices=tuple(VALID_APPEARANCES),
        default="default",
    )
    parser.add_argument("--overwrite", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    result = convert_pokemon_gif(
        args.gif_path,
        args.output_root,
        pokemon_name=args.pokemon_name,
        view=args.view,
        appearance=args.appearance,
        overwrite=args.overwrite,
    )
    print(
        json.dumps(
            {
                "atlas_path": str(result.atlas_path),
                "manifest_path": str(result.manifest_path),
                "reused_existing_output": result.reused_existing_output,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
