"""Download and rename every Generation V animated Pokemon GIF.

The downloader reads a pinned snapshot of PokeAPI's sprites repository, maps
numeric sprite identifiers through PokeAPI, separates visual variants into
folders, verifies every Git blob, and writes CSV/JSON traceability manifests.
It runs in normal system Python and does not require Unreal Editor.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import sys
import time
from concurrent.futures import Future, ThreadPoolExecutor, as_completed
from dataclasses import dataclass, replace
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any, Iterable
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


GITHUB_OWNER = "PokeAPI"
GITHUB_REPOSITORY = "sprites"
GITHUB_BRANCH = "master"
GITHUB_API_VERSION = "2022-11-28"
GENERATION_DIRECTORY_NAME = "generation-v"
REPOSITORY_GENERATION_ROOT = PurePosixPath(
    "sprites/pokemon/versions/generation-v"
)
ANIMATED_ROOT = PurePosixPath("black-white/animated")
ALLOWED_ANIMATED_SUBFOLDERS = frozenset({"back", "female", "shiny"})
POKEAPI_POKEMON_URL = "https://pokeapi.co/api/v2/pokemon?limit=100000&offset=0"
POKEAPI_SPECIES_URL = (
    "https://pokeapi.co/api/v2/pokemon-species?limit=100000&offset=0"
)
USER_AGENT = "Pokemon-Solarus-Generation-V-Sprite-Downloader/1.0"
DEFAULT_WORKERS = 8
DEFAULT_OUTPUT_DIRECTORY = (
    Path(__file__).resolve().parents[2]
    / "Content"
    / "Art"
    / "GIF"
    / GENERATION_DIRECTORY_NAME
)


class DownloaderError(RuntimeError):
    """Raised when the sprite catalog or a downloaded file is unsafe."""


@dataclass(frozen=True)
class DownloadRecord:
    source_path: str
    source_stem: str
    pokemon_id: int | None
    pokemon_name: str
    mapping_type: str
    appearance: str
    view: str
    blob_sha: str
    size_bytes: int
    raw_url: str
    target_relative_path: PurePosixPath
    is_duplicate_alias: bool = False
    canonical_source_path: str = ""


@dataclass(frozen=True)
class RepositorySnapshot:
    commit_sha: str
    generation_tree_sha: str
    tree_entries: list[dict[str, Any]]


@dataclass(frozen=True)
class DownloadOutcome:
    status: str
    size_bytes: int


def _request_bytes(
    url: str,
    *,
    accept: str | None = None,
    timeout_seconds: float = 60.0,
    attempts: int = 4,
) -> bytes:
    headers = {"User-Agent": USER_AGENT}
    if accept:
        headers["Accept"] = accept
    if url.startswith("https://api.github.com/"):
        headers["X-GitHub-Api-Version"] = GITHUB_API_VERSION

    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        request = Request(url, headers=headers)
        try:
            with urlopen(request, timeout=timeout_seconds) as response:
                return response.read()
        except HTTPError as exc:
            last_error = exc
            retryable = exc.code in {408, 429, 500, 502, 503, 504}
            if not retryable or attempt == attempts:
                detail = exc.read(500).decode("utf-8", errors="replace").strip()
                raise DownloaderError(
                    f"HTTP {exc.code} while reading {url}: {detail or exc.reason}"
                ) from exc
        except (TimeoutError, URLError, OSError) as exc:
            last_error = exc
            if attempt == attempts:
                break

        time.sleep(min(2 ** (attempt - 1), 8))

    raise DownloaderError(f"Failed to read {url}: {last_error}") from last_error


def _request_json(url: str) -> Any:
    accept = (
        "application/vnd.github+json"
        if url.startswith("https://api.github.com/")
        else "application/json"
    )
    try:
        return json.loads(
            _request_bytes(
                url,
                accept=accept,
            ).decode("utf-8")
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise DownloaderError(f"Endpoint did not return valid JSON: {url}") from exc


def _github_api_url(path: str) -> str:
    return f"https://api.github.com/repos/{GITHUB_OWNER}/{GITHUB_REPOSITORY}/{path}"


def load_repository_snapshot() -> RepositorySnapshot:
    commit = _request_json(_github_api_url(f"commits/{GITHUB_BRANCH}"))
    commit_sha = str(commit.get("sha", ""))
    if re.fullmatch(r"[0-9a-f]{40}", commit_sha) is None:
        raise DownloaderError("GitHub did not return a valid repository commit SHA.")

    versions_url = _github_api_url(
        "contents/sprites/pokemon/versions?ref=" + quote(commit_sha, safe="")
    )
    versions = _request_json(versions_url)
    if not isinstance(versions, list):
        raise DownloaderError("GitHub did not return the versions directory listing.")

    generation = next(
        (
            entry
            for entry in versions
            if entry.get("type") == "dir"
            and entry.get("name") == GENERATION_DIRECTORY_NAME
        ),
        None,
    )
    if generation is None:
        raise DownloaderError(
            f"GitHub does not contain {GENERATION_DIRECTORY_NAME}."
        )

    generation_tree_sha = str(generation.get("sha", ""))
    if re.fullmatch(r"[0-9a-f]{40}", generation_tree_sha) is None:
        raise DownloaderError("GitHub returned an invalid Generation V tree SHA.")

    tree = _request_json(
        _github_api_url(f"git/trees/{generation_tree_sha}?recursive=1")
    )
    if tree.get("truncated") is True:
        raise DownloaderError(
            "GitHub truncated the Generation V tree; refusing an incomplete download."
        )
    tree_entries = tree.get("tree")
    if not isinstance(tree_entries, list):
        raise DownloaderError("GitHub did not return a readable Generation V tree.")

    return RepositorySnapshot(
        commit_sha=commit_sha,
        generation_tree_sha=generation_tree_sha,
        tree_entries=tree_entries,
    )


def _resource_id(resource_url: str) -> int:
    match = re.search(r"/(\d+)/?$", resource_url)
    if match is None:
        raise DownloaderError(f"PokeAPI resource URL has no numeric ID: {resource_url}")
    return int(match.group(1))


def load_pokeapi_name_map(url: str) -> dict[int, str]:
    names_by_id: dict[int, str] = {}
    next_url: str | None = url
    while next_url:
        payload = _request_json(next_url)
        results = payload.get("results")
        if not isinstance(results, list):
            raise DownloaderError(f"PokeAPI returned no resource list: {next_url}")
        for result in results:
            name = str(result.get("name", ""))
            resource_url = str(result.get("url", ""))
            if re.fullmatch(r"[a-z0-9-]+", name) is None:
                raise DownloaderError(f"PokeAPI returned an unsafe name: {name!r}")
            resource_id = _resource_id(resource_url)
            existing = names_by_id.get(resource_id)
            if existing is not None and existing != name:
                raise DownloaderError(
                    f"PokeAPI ID {resource_id} maps to both {existing} and {name}."
                )
            names_by_id[resource_id] = name

        raw_next = payload.get("next")
        next_url = str(raw_next) if raw_next else None

    return names_by_id


def classify_source_path(source_path: str) -> tuple[str, str, str]:
    path = PurePosixPath(source_path)
    try:
        relative = path.relative_to(ANIMATED_ROOT)
    except ValueError as exc:
        raise DownloaderError(
            f"GIF is outside the Generation V animated folder: {source_path}"
        ) from exc

    if relative.suffix.lower() != ".gif" or len(relative.parts) < 1:
        raise DownloaderError(f"Expected an animated GIF path: {source_path}")

    subfolders = set(relative.parts[:-1])
    unknown_subfolders = subfolders - ALLOWED_ANIMATED_SUBFOLDERS
    if unknown_subfolders:
        raise DownloaderError(
            f"Unsupported animated GIF subfolder in {source_path}: "
            + ", ".join(sorted(unknown_subfolders))
        )

    view = "back" if "back" in subfolders else "front"
    is_shiny = "shiny" in subfolders
    is_female = "female" in subfolders
    if is_shiny and is_female:
        appearance = "shiny-female"
    elif is_shiny:
        appearance = "shiny"
    elif is_female:
        appearance = "female"
    else:
        appearance = "default"

    return appearance, view, relative.stem.lower()


def resolve_pokemon_name(
    source_stem: str,
    pokemon_names_by_id: dict[int, str],
    species_names_by_id: dict[int, str],
) -> tuple[str, str, int | None]:
    if source_stem.isdigit():
        pokemon_id = int(source_stem)
        pokemon_name = pokemon_names_by_id.get(pokemon_id)
        if pokemon_name is None:
            raise DownloaderError(
                f"No PokeAPI Pokemon name exists for sprite ID {pokemon_id}."
            )
        mapping_type = "pokemon-form-id" if pokemon_id >= 10000 else "pokemon-id"
        return pokemon_name, mapping_type, pokemon_id

    form_match = re.fullmatch(r"(\d+)-([a-z0-9-]+)", source_stem)
    if form_match:
        species_id = int(form_match.group(1))
        form_suffix = form_match.group(2)
        species_name = species_names_by_id.get(species_id)
        if species_name is None:
            raise DownloaderError(
                f"No PokeAPI species name exists for sprite form {source_stem}."
            )
        return (
            f"{species_name}-{form_suffix}",
            "species-form-filename",
            species_id,
        )

    if re.fullmatch(r"[a-z0-9-]+", source_stem) is None:
        raise DownloaderError(f"Unsafe repository sprite name: {source_stem!r}")
    return source_stem, "repository-name", None


def _raw_url(commit_sha: str, source_path: str) -> str:
    repository_path = REPOSITORY_GENERATION_ROOT / PurePosixPath(source_path)
    encoded_path = quote(repository_path.as_posix(), safe="/")
    return (
        f"https://raw.githubusercontent.com/{GITHUB_OWNER}/{GITHUB_REPOSITORY}/"
        f"{commit_sha}/{encoded_path}"
    )


def _collision_target(record: DownloadRecord, used_targets: set[str]) -> PurePosixPath:
    suffix = re.sub(r"[^a-z0-9-]+", "-", record.source_stem).strip("-")
    candidate = PurePosixPath(record.appearance) / (
        f"{record.pokemon_name}-source-{suffix}_{record.view}.gif"
    )
    if candidate.as_posix().casefold() not in used_targets:
        return candidate
    return PurePosixPath(record.appearance) / (
        f"{record.pokemon_name}-source-{suffix}-{record.blob_sha[:8]}_"
        f"{record.view}.gif"
    )


def build_download_records(
    tree_entries: Iterable[dict[str, Any]],
    *,
    commit_sha: str,
    pokemon_names_by_id: dict[int, str],
    species_names_by_id: dict[int, str],
) -> list[DownloadRecord]:
    records: list[DownloadRecord] = []
    animated_prefix = ANIMATED_ROOT.as_posix() + "/"

    gif_entries = sorted(
        (
            entry
            for entry in tree_entries
            if entry.get("type") == "blob"
            and str(entry.get("path", "")).startswith(animated_prefix)
            and str(entry.get("path", "")).lower().endswith(".gif")
        ),
        key=lambda entry: str(entry["path"]).casefold(),
    )
    if not gif_entries:
        raise DownloaderError("The Generation V tree contains no animated GIFs.")

    for entry in gif_entries:
        source_path = str(entry["path"])
        blob_sha = str(entry.get("sha", ""))
        size_bytes = int(entry.get("size", -1))
        if re.fullmatch(r"[0-9a-f]{40}", blob_sha) is None:
            raise DownloaderError(f"Invalid Git blob SHA for {source_path}: {blob_sha}")
        if size_bytes <= 0:
            raise DownloaderError(f"Invalid Git blob size for {source_path}: {size_bytes}")

        appearance, view, source_stem = classify_source_path(source_path)
        pokemon_name, mapping_type, pokemon_id = resolve_pokemon_name(
            source_stem,
            pokemon_names_by_id,
            species_names_by_id,
        )
        target_relative_path = PurePosixPath(appearance) / (
            f"{pokemon_name}_{view}.gif"
        )
        records.append(
            DownloadRecord(
                source_path=source_path,
                source_stem=source_stem,
                pokemon_id=pokemon_id,
                pokemon_name=pokemon_name,
                mapping_type=mapping_type,
                appearance=appearance,
                view=view,
                blob_sha=blob_sha,
                size_bytes=size_bytes,
                raw_url=_raw_url(commit_sha, source_path),
                target_relative_path=target_relative_path,
            )
        )

    groups: dict[str, list[DownloadRecord]] = {}
    for record in records:
        key = record.target_relative_path.as_posix().casefold()
        groups.setdefault(key, []).append(record)

    finalized: list[DownloadRecord] = []
    used_targets: set[str] = set()
    for target_key in sorted(groups):
        target_group = sorted(groups[target_key], key=lambda item: item.source_path)
        by_blob: dict[str, list[DownloadRecord]] = {}
        for record in target_group:
            by_blob.setdefault(record.blob_sha, []).append(record)

        blob_groups = sorted(
            by_blob.values(),
            key=lambda group: group[0].source_path,
        )
        for blob_index, blob_group in enumerate(blob_groups):
            canonical = blob_group[0]
            if blob_index == 0:
                target = canonical.target_relative_path
            else:
                target = _collision_target(canonical, used_targets)
            target_key_for_blob = target.as_posix().casefold()
            if target_key_for_blob in used_targets:
                raise DownloaderError(f"Could not resolve target collision: {target}")
            used_targets.add(target_key_for_blob)

            for alias_index, record in enumerate(blob_group):
                finalized.append(
                    replace(
                        record,
                        target_relative_path=target,
                        is_duplicate_alias=alias_index > 0,
                        canonical_source_path=canonical.source_path,
                    )
                )

    return sorted(
        finalized,
        key=lambda record: (
            record.target_relative_path.as_posix().casefold(),
            record.source_path.casefold(),
        ),
    )


def _git_blob_sha(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def _validate_gif_data(record: DownloadRecord, data: bytes) -> None:
    if len(data) != record.size_bytes:
        raise DownloaderError(
            f"Size mismatch for {record.source_path}: "
            f"expected {record.size_bytes}, received {len(data)}"
        )
    if not data.startswith((b"GIF87a", b"GIF89a")):
        raise DownloaderError(f"Downloaded file is not a GIF: {record.source_path}")
    actual_sha = _git_blob_sha(data)
    if actual_sha != record.blob_sha:
        raise DownloaderError(
            f"Git blob mismatch for {record.source_path}: "
            f"expected {record.blob_sha}, received {actual_sha}"
        )


def _target_path(output_directory: Path, record: DownloadRecord) -> Path:
    return output_directory.joinpath(*record.target_relative_path.parts)


def _download_one(
    record: DownloadRecord,
    output_directory: Path,
    *,
    overwrite: bool,
) -> DownloadOutcome:
    target_path = _target_path(output_directory, record)
    if target_path.is_file():
        existing_data = target_path.read_bytes()
        try:
            _validate_gif_data(record, existing_data)
            return DownloadOutcome("reused", len(existing_data))
        except DownloaderError:
            if not overwrite:
                raise DownloaderError(
                    f"Existing file does not match its source: {target_path}. "
                    "Use --overwrite to replace it."
                )

    data = _request_bytes(record.raw_url)
    _validate_gif_data(record, data)
    target_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = target_path.with_suffix(target_path.suffix + ".part")
    temporary_path.write_bytes(data)
    temporary_path.replace(target_path)
    return DownloadOutcome("downloaded", len(data))


def _unique_download_records(records: Iterable[DownloadRecord]) -> list[DownloadRecord]:
    unique: dict[str, DownloadRecord] = {}
    for record in records:
        if record.is_duplicate_alias:
            continue
        key = record.target_relative_path.as_posix().casefold()
        if key in unique:
            raise DownloaderError(
                f"Multiple downloads target the same output path: "
                f"{record.target_relative_path}"
            )
        unique[key] = record
    return [unique[key] for key in sorted(unique)]


def _write_csv(path: Path, fieldnames: list[str], rows: Iterable[dict[str, Any]]) -> None:
    temporary_path = path.with_suffix(path.suffix + ".tmp")
    with temporary_path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    temporary_path.replace(path)


def write_name_map(output_directory: Path, records: list[DownloadRecord]) -> Path:
    path = output_directory / "pokemon_name_map.csv"
    unique: dict[str, DownloadRecord] = {}
    for record in records:
        unique.setdefault(record.source_stem, record)
    rows = (
        {
            "source_stem": record.source_stem,
            "pokemon_id": "" if record.pokemon_id is None else record.pokemon_id,
            "pokemon_name": record.pokemon_name,
            "mapping_type": record.mapping_type,
        }
        for record in sorted(unique.values(), key=lambda item: item.source_stem)
    )
    _write_csv(
        path,
        ["source_stem", "pokemon_id", "pokemon_name", "mapping_type"],
        rows,
    )
    return path


def write_download_manifest(
    output_directory: Path,
    records: list[DownloadRecord],
) -> Path:
    path = output_directory / "download_manifest.csv"
    rows = (
        {
            "source_path": record.source_path,
            "source_stem": record.source_stem,
            "pokemon_id": "" if record.pokemon_id is None else record.pokemon_id,
            "pokemon_name": record.pokemon_name,
            "appearance": record.appearance,
            "view": record.view,
            "target_path": record.target_relative_path.as_posix(),
            "blob_sha": record.blob_sha,
            "size_bytes": record.size_bytes,
            "is_duplicate_alias": str(record.is_duplicate_alias).lower(),
            "canonical_source_path": record.canonical_source_path,
            "source_url": record.raw_url,
        }
        for record in records
    )
    _write_csv(
        path,
        [
            "source_path",
            "source_stem",
            "pokemon_id",
            "pokemon_name",
            "appearance",
            "view",
            "target_path",
            "blob_sha",
            "size_bytes",
            "is_duplicate_alias",
            "canonical_source_path",
            "source_url",
        ],
        rows,
    )
    return path


def _verify_outputs(
    output_directory: Path,
    records: list[DownloadRecord],
) -> int:
    verified_bytes = 0
    for record in records:
        target_path = _target_path(output_directory, record)
        if not target_path.is_file():
            raise DownloaderError(f"Downloaded GIF is missing: {target_path}")
        data = target_path.read_bytes()
        _validate_gif_data(record, data)
        verified_bytes += len(data)
    return verified_bytes


def _download_all(
    records: list[DownloadRecord],
    output_directory: Path,
    *,
    overwrite: bool,
    workers: int,
) -> dict[str, int]:
    downloaded = 0
    reused = 0
    completed_bytes = 0
    total = len(records)
    last_report = time.monotonic()

    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures: dict[Future[DownloadOutcome], DownloadRecord] = {
            executor.submit(
                _download_one,
                record,
                output_directory,
                overwrite=overwrite,
            ): record
            for record in records
        }
        try:
            for completed, future in enumerate(as_completed(futures), start=1):
                outcome = future.result()
                if outcome.status == "downloaded":
                    downloaded += 1
                else:
                    reused += 1
                completed_bytes += outcome.size_bytes

                now = time.monotonic()
                if completed == total or completed % 100 == 0 or now - last_report >= 5:
                    print(
                        f"Progress: {completed}/{total} files "
                        f"({completed_bytes / (1024 * 1024):.1f} MiB)",
                        flush=True,
                    )
                    last_report = now
        except Exception:
            for future in futures:
                future.cancel()
            raise

    return {"downloaded": downloaded, "reused": reused}


def _write_summary(output_directory: Path, summary: dict[str, Any]) -> Path:
    path = output_directory / "download_summary.json"
    temporary_path = path.with_suffix(path.suffix + ".tmp")
    temporary_path.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary_path.replace(path)
    return path


def download_generation_v_gifs(
    output_directory: str | Path = DEFAULT_OUTPUT_DIRECTORY,
    *,
    overwrite: bool = False,
    workers: int = DEFAULT_WORKERS,
    dry_run: bool = False,
) -> dict[str, Any]:
    """Plan, download, rename, and verify all Generation V animated GIFs."""

    if workers < 1 or workers > 32:
        raise DownloaderError("workers must be between 1 and 32.")
    destination = Path(output_directory).expanduser().resolve()

    print("Reading the pinned PokeAPI sprites repository tree...", flush=True)
    snapshot = load_repository_snapshot()
    print("Reading Pokemon and species names from PokeAPI...", flush=True)
    pokemon_names = load_pokeapi_name_map(POKEAPI_POKEMON_URL)
    species_names = load_pokeapi_name_map(POKEAPI_SPECIES_URL)
    records = build_download_records(
        snapshot.tree_entries,
        commit_sha=snapshot.commit_sha,
        pokemon_names_by_id=pokemon_names,
        species_names_by_id=species_names,
    )
    downloads = _unique_download_records(records)

    source_bytes = sum(record.size_bytes for record in records)
    unique_bytes = sum(record.size_bytes for record in downloads)
    duplicate_alias_count = sum(record.is_duplicate_alias for record in records)
    plan = {
        "source_entry_count": len(records),
        "unique_file_count": len(downloads),
        "duplicate_alias_count": duplicate_alias_count,
        "source_bytes_including_aliases": source_bytes,
        "unique_download_bytes": unique_bytes,
        "output_directory": str(destination),
        "repository_commit_sha": snapshot.commit_sha,
        "generation_tree_sha": snapshot.generation_tree_sha,
    }
    print(json.dumps(plan, indent=2, sort_keys=True), flush=True)
    if dry_run:
        return {**plan, "dry_run": True}

    destination.mkdir(parents=True, exist_ok=True)
    name_map_path = write_name_map(destination, records)
    manifest_path = write_download_manifest(destination, records)
    outcomes = _download_all(
        downloads,
        destination,
        overwrite=overwrite,
        workers=workers,
    )
    verified_bytes = _verify_outputs(destination, downloads)
    summary = {
        **plan,
        **outcomes,
        "verified_file_count": len(downloads),
        "verified_bytes": verified_bytes,
        "pokemon_name_map": str(name_map_path),
        "download_manifest": str(manifest_path),
        "source_repository": f"https://github.com/{GITHUB_OWNER}/{GITHUB_REPOSITORY}",
        "source_license": (
            f"https://github.com/{GITHUB_OWNER}/{GITHUB_REPOSITORY}/blob/"
            f"{snapshot.commit_sha}/LICENCE.txt"
        ),
        "completed_at_utc": datetime.now(timezone.utc).isoformat(),
    }
    summary["download_summary"] = str(destination / "download_summary.json")
    summary_path = _write_summary(destination, summary)
    if summary_path != destination / "download_summary.json":
        raise DownloaderError(f"Unexpected summary path: {summary_path}")
    return summary


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Download every Generation V animated GIF, rename it with its "
            "Pokemon/form name, and separate default/shiny/female variants."
        )
    )
    parser.add_argument(
        "--output-directory",
        type=Path,
        default=DEFAULT_OUTPUT_DIRECTORY,
        help=f"Destination directory (default: {DEFAULT_OUTPUT_DIRECTORY})",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Replace an existing GIF only when it fails source verification.",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=DEFAULT_WORKERS,
        help=f"Parallel downloads from 1 to 32 (default: {DEFAULT_WORKERS}).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Read and validate the live catalogs without writing files.",
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    try:
        result = download_generation_v_gifs(
            args.output_directory,
            overwrite=args.overwrite,
            workers=args.workers,
            dry_run=args.dry_run,
        )
    except DownloaderError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Download interrupted. Run the command again to resume safely.", file=sys.stderr)
        return 130

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
