from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image


TOOL_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIRECTORY))

from convert_pokemon_gif import (  # noqa: E402
    GifConversionError,
    convert_pokemon_gif,
    parse_gif_identity,
)


class PokemonGifConverterTests(unittest.TestCase):
    def test_identity_uses_front_back_suffix_and_corrects_current_typo(self) -> None:
        charizard = parse_gif_identity("charizard_back.gif")
        bulbasaur = parse_gif_identity("bulbasaur_front.gif")
        venusaur = parse_gif_identity("venasaur_front.gif")

        self.assertEqual((charizard.pokemon, charizard.view), ("Charizard", "Back"))
        self.assertEqual((bulbasaur.pokemon, bulbasaur.view), ("Bulbasaur", "Front"))
        self.assertEqual((venusaur.pokemon, venusaur.view), ("Venusaur", "Front"))

    def test_identity_rejects_a_file_without_view_suffix(self) -> None:
        with self.assertRaises(GifConversionError):
            parse_gif_identity("charizard.gif")

        with self.assertRaises(GifConversionError):
            parse_gif_identity("charizard-front.gif")

    def test_conversion_deduplicates_frames_and_preserves_timing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            gif_path = root / "testmon_front.gif"
            output_root = root / "output"

            red = Image.new("RGB", (4, 3), (255, 0, 0))
            blue = Image.new("RGB", (4, 3), (0, 0, 255))
            red.save(
                gif_path,
                save_all=True,
                append_images=[blue, red],
                duration=[100, 200, 100],
                loop=0,
                disposal=2,
            )

            result = convert_pokemon_gif(gif_path, output_root)
            manifest = json.loads(result.manifest_path.read_text(encoding="utf-8"))

            self.assertTrue(result.atlas_path.is_file())
            self.assertEqual(manifest["source_frame_count"], 3)
            self.assertEqual(manifest["unique_sprite_count"], 2)
            self.assertEqual(manifest["frames_per_second"], 10.0)
            self.assertEqual(manifest["duration_ms"], 400)
            self.assertEqual(
                manifest["key_frames"],
                [
                    {"frame_run": 1, "sprite_index": 0},
                    {"frame_run": 2, "sprite_index": 1},
                    {"frame_run": 1, "sprite_index": 0},
                ],
            )

            second_result = convert_pokemon_gif(gif_path, output_root)
            self.assertTrue(second_result.reused_existing_output)


if __name__ == "__main__":
    unittest.main()
