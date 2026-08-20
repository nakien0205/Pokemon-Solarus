from __future__ import annotations

import importlib.util
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch


GAME_DIRECTORY = Path(__file__).resolve().parents[3]
IMPORTER_PATH = GAME_DIRECTORY / "Content" / "Python" / "pokemon_sprite_importer.py"


def _load_importer_module() -> types.ModuleType:
    unreal_stub = types.ModuleType("unreal")
    previous_unreal = sys.modules.get("unreal")
    sys.modules["unreal"] = unreal_stub
    try:
        spec = importlib.util.spec_from_file_location(
            "pokemon_sprite_importer_for_tests",
            IMPORTER_PATH,
        )
        if spec is None or spec.loader is None:
            raise RuntimeError(f"Could not load importer: {IMPORTER_PATH}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        if previous_unreal is None:
            sys.modules.pop("unreal", None)
        else:
            sys.modules["unreal"] = previous_unreal


IMPORTER = _load_importer_module()


class PokemonSpriteFolderImportTests(unittest.TestCase):
    def test_folder_import_uses_only_direct_gifs_in_stable_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            folder = Path(temporary_directory)
            (folder / "charmander_back.gif").touch()
            (folder / "bulbasaur_front.GIF").touch()
            (folder / "notes.txt").touch()
            nested = folder / "nested"
            nested.mkdir()
            (nested / "squirtle_front.gif").touch()
            expected_result = [{"pokemon": "Bulbasaur"}, {"pokemon": "Charmander"}]

            with patch.object(
                IMPORTER,
                "import_pokemon_gifs",
                return_value=expected_result,
            ) as import_many:
                result = IMPORTER.import_pokemon_gif_folder(
                    str(folder),
                    replace_existing=True,
                )

            imported_paths = import_many.call_args.args[0]
            self.assertEqual(
                [Path(path).name for path in imported_paths],
                ["bulbasaur_front.GIF", "charmander_back.gif"],
            )
            self.assertEqual(
                import_many.call_args.kwargs,
                {
                    "destination_root": IMPORTER.DEFAULT_DESTINATION_ROOT,
                    "pixels_per_unreal_unit": (
                        IMPORTER.DEFAULT_PIXELS_PER_UNREAL_UNIT
                    ),
                    "replace_existing": True,
                    "project_directory_override": None,
                    "source_output_root_override": None,
                },
            )
            self.assertEqual(result, expected_result)

    def test_folder_import_rejects_all_invalid_gif_names_before_import(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            folder = Path(temporary_directory)
            (folder / "bulbasaur_front.gif").touch()
            (folder / "missing-view.gif").touch()
            (folder / "charmander-front.gif").touch()

            with patch.object(IMPORTER, "import_pokemon_gifs") as import_many:
                with self.assertRaisesRegex(
                    IMPORTER.PokemonSpriteImportError,
                    "charmander-front.gif.*missing-view.gif",
                ):
                    IMPORTER.import_pokemon_gif_folder(str(folder))

            import_many.assert_not_called()

    def test_folder_import_rejects_a_folder_without_gifs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            folder = Path(temporary_directory)
            (folder / "notes.txt").touch()

            with patch.object(IMPORTER, "import_pokemon_gifs") as import_many:
                with self.assertRaisesRegex(
                    IMPORTER.PokemonSpriteImportError,
                    "does not contain any GIF files",
                ):
                    IMPORTER.import_pokemon_gif_folder(str(folder))

            import_many.assert_not_called()

    def test_folder_import_rejects_a_non_directory_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            file_path = Path(temporary_directory) / "bulbasaur_front.gif"
            file_path.touch()

            with self.assertRaisesRegex(
                IMPORTER.PokemonSpriteImportError,
                "Folder does not exist",
            ):
                IMPORTER.import_pokemon_gif_folder(str(file_path))


if __name__ == "__main__":
    unittest.main()
