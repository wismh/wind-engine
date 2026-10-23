#!/usr/bin/env python3
"""Tests of split_symbols.py: python tools/ci/test_split_symbols.py"""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import split_symbols  # noqa: E402

# A stand-in for `strip`: cuts the file to a quarter of its size.
FAKE_STRIP = Path(__file__).resolve().parent / "fake_strip_for_tests.py"


def quiet(function, *args, **kwargs):
    with contextlib.redirect_stderr(io.StringIO()):
        return function(*args, **kwargs)


def make_sdk(root, os_name="windows"):
    sdk = Path(root) / "sdk"
    (sdk / "bin").mkdir(parents=True)
    (sdk / "sdk.toml").write_text('version = "0.1.7a"\nbuild_id = "abc"\nplatform = "%s"\n' % os_name, encoding="utf-8")
    (sdk / "bin" / "engine.pdb").write_bytes(b"p" * 10)
    (sdk / "bin" / "wind_editor.exe").write_bytes(b"MZ")
    (sdk / "player" / "Development" / "lib").mkdir(parents=True)
    (sdk / "player" / "Development" / "lib" / "engine.lib").write_bytes(b"d" * 4000)
    (sdk / "player" / "Shipping" / "lib").mkdir(parents=True)
    (sdk / "player" / "Shipping" / "lib" / "engine.lib").write_bytes(b"s" * 4000)
    (sdk / "lib").mkdir()
    (sdk / "lib" / "gtest.lib").write_bytes(b"g" * 4000)
    return sdk


def strip_command():
    return [sys.executable, str(FAKE_STRIP)]


class Split(unittest.TestCase):
    def setUp(self):
        self.min_saving = split_symbols.MIN_SAVING
        split_symbols.MIN_SAVING = 100
        FAKE_STRIP.write_text(
            "import sys\n"
            "from pathlib import Path\n"
            "path = Path(sys.argv[-1])\n"
            "data = path.read_bytes()\n"
            "keep = len(data) if data[:1] == b's' else len(data) // 4\n"
            "path.write_bytes(data[:keep])\n", encoding="utf-8")

    def tearDown(self):
        split_symbols.MIN_SAVING = self.min_saving
        FAKE_STRIP.unlink()

    def test_windows_moves_pdb_and_swaps_the_libraries_that_shrink(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = make_sdk(directory)
            symbols = Path(directory) / "symbols"
            moved = quiet(split_symbols.split, "windows", sdk, symbols, strip_command())
            self.assertEqual({path.as_posix() for path in moved},
                             {"bin/engine.pdb", "player/Development/lib/engine.lib"})
            self.assertFalse((sdk / "bin" / "engine.pdb").exists())
            self.assertEqual((symbols / "bin" / "engine.pdb").stat().st_size, 10)
            self.assertEqual((symbols / "player/Development/lib/engine.lib").stat().st_size, 4000)
            self.assertEqual((sdk / "player/Development/lib/engine.lib").stat().st_size, 1000)
            # A library that barely shrinks, the import and test libraries, and the programs stay.
            self.assertEqual((sdk / "player/Shipping/lib/engine.lib").stat().st_size, 4000)
            self.assertFalse((symbols / "player/Shipping").exists())
            self.assertEqual((sdk / "lib/gtest.lib").stat().st_size, 4000)
            self.assertTrue((sdk / "bin" / "wind_editor.exe").is_file())

    def test_a_small_saving_is_not_worth_a_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = make_sdk(directory)
            split_symbols.MIN_SAVING = 10000
            moved = quiet(split_symbols.split, "windows", sdk, Path(directory) / "symbols", strip_command())
            self.assertEqual([path.as_posix() for path in moved], ["bin/engine.pdb"])

    def test_the_manifest_names_the_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = make_sdk(directory)
            symbols = Path(directory) / "symbols"
            quiet(split_symbols.split, "windows", sdk, symbols, strip_command())
            text = (symbols / "symbols.toml").read_text(encoding="utf-8")
            self.assertIn('version = "0.1.7a"', text)
            self.assertIn('build_id = "abc"', text)
            self.assertIn('platform = "windows"', text)

    def test_nothing_to_split_makes_no_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = make_sdk(directory)
            (sdk / "bin" / "engine.pdb").unlink()
            (sdk / "player" / "Development" / "lib" / "engine.lib").unlink()
            symbols = Path(directory) / "symbols"
            self.assertEqual(quiet(split_symbols.split, "windows", sdk, symbols, strip_command()), [])
            self.assertFalse(symbols.exists())

    def test_refuses_an_sdk_of_another_platform(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = make_sdk(directory, "linux")
            with self.assertRaisesRegex(split_symbols.SplitError, "is for linux, not windows"):
                quiet(split_symbols.split, "windows", sdk, Path(directory) / "symbols", strip_command())

    def test_refuses_a_directory_that_is_no_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(split_symbols.SplitError, "no sdk.toml"):
                quiet(split_symbols.split, "windows", directory, Path(directory) / "symbols", strip_command())

    def test_native_binaries_are_found_by_their_magic(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = make_sdk(directory, "linux")
            (sdk / "bin" / "wind_editor").write_bytes(b"\x7fELF" + b"\0" * 8)
            (sdk / "bin" / "libengine.dylib").write_bytes(b"\xcf\xfa\xed\xfe" + b"\0" * 8)
            (sdk / "bin" / "notes.txt").write_text("text", encoding="utf-8")
            names = [path.name for path in split_symbols.native_binaries(sdk)]
            self.assertEqual(names, ["libengine.dylib", "wind_editor"])


if __name__ == "__main__":
    unittest.main()
