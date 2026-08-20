from __future__ import annotations

import sys
import unittest
from pathlib import Path, PurePosixPath


TOOL_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIRECTORY))

from download_generation_v_gifs import (  # noqa: E402
    DownloaderError,
    build_download_records,
    classify_source_path,
    resolve_pokemon_name,
)


class GenerationVGifDownloaderTests(unittest.TestCase):
    def test_classify_source_path_maps_every_supported_variant(self) -> None:
        cases = {
            "black-white/animated/1.gif": ("default", "front", "1"),
            "black-white/animated/back/1.gif": ("default", "back", "1"),
            "black-white/animated/shiny/1.gif": ("shiny", "front", "1"),
            "black-white/animated/back/shiny/1.gif": (
                "shiny",
                "back",
                "1",
            ),
            "black-white/animated/female/25.gif": (
                "female",
                "front",
                "25",
            ),
            "black-white/animated/back/female/25.gif": (
                "female",
                "back",
                "25",
            ),
            "black-white/animated/shiny/female/25.gif": (
                "shiny-female",
                "front",
                "25",
            ),
            "black-white/animated/back/shiny/female/25.gif": (
                "shiny-female",
                "back",
                "25",
            ),
        }

        for source_path, expected in cases.items():
            with self.subTest(source_path=source_path):
                self.assertEqual(classify_source_path(source_path), expected)

    def test_classify_source_path_rejects_unknown_subfolders(self) -> None:
        with self.assertRaises(DownloaderError):
            classify_source_path("black-white/animated/icons/1.gif")

        with self.assertRaises(DownloaderError):
            classify_source_path("black-white/1.gif")

    def test_resolve_pokemon_name_supports_ids_forms_and_substitute(self) -> None:
        pokemon_names = {
            1: "bulbasaur",
            386: "deoxys-normal",
            10001: "deoxys-attack",
        }
        species_names = {
            201: "unown",
            386: "deoxys",
        }

        self.assertEqual(
            resolve_pokemon_name("1", pokemon_names, species_names),
            ("bulbasaur", "pokemon-id", 1),
        )
        self.assertEqual(
            resolve_pokemon_name("10001", pokemon_names, species_names),
            ("deoxys-attack", "pokemon-form-id", 10001),
        )
        self.assertEqual(
            resolve_pokemon_name("201-a", pokemon_names, species_names),
            ("unown-a", "species-form-filename", 201),
        )
        self.assertEqual(
            resolve_pokemon_name("386-attack", pokemon_names, species_names),
            ("deoxys-attack", "species-form-filename", 386),
        )
        self.assertEqual(
            resolve_pokemon_name("substitute", pokemon_names, species_names),
            ("substitute", "repository-name", None),
        )

    def test_build_records_deduplicates_aliases_with_the_same_blob(self) -> None:
        entries = [
            {
                "path": "black-white/animated/10001.gif",
                "sha": "a" * 40,
                "size": 100,
                "type": "blob",
            },
            {
                "path": "black-white/animated/386-attack.gif",
                "sha": "a" * 40,
                "size": 100,
                "type": "blob",
            },
        ]

        records = build_download_records(
            entries,
            commit_sha="commit-sha",
            pokemon_names_by_id={10001: "deoxys-attack"},
            species_names_by_id={386: "deoxys"},
        )

        self.assertEqual(len(records), 2)
        self.assertEqual(
            {record.target_relative_path for record in records},
            {PurePosixPath("default/deoxys-attack_front.gif")},
        )
        self.assertIn(
            "/sprites/pokemon/versions/generation-v/black-white/animated/",
            records[0].raw_url,
        )
        self.assertEqual(sum(record.is_duplicate_alias for record in records), 1)

    def test_build_records_preserves_different_blobs_after_name_collision(self) -> None:
        entries = [
            {
                "path": "black-white/animated/386.gif",
                "sha": "b" * 40,
                "size": 100,
                "type": "blob",
            },
            {
                "path": "black-white/animated/386-normal.gif",
                "sha": "c" * 40,
                "size": 101,
                "type": "blob",
            },
        ]

        records = build_download_records(
            entries,
            commit_sha="commit-sha",
            pokemon_names_by_id={386: "deoxys-normal"},
            species_names_by_id={386: "deoxys"},
        )

        self.assertEqual(len(records), 2)
        self.assertEqual(len({record.target_relative_path for record in records}), 2)
        for record in records:
            self.assertTrue(record.target_relative_path.name.endswith("_front.gif"))
            self.assertFalse(record.is_duplicate_alias)


if __name__ == "__main__":
    unittest.main()
