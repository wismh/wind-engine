#!/usr/bin/env python3
"""Tests of setup_sdk.py: python tools/ci/test_setup_sdk.py"""

import contextlib
import hashlib
import io
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import setup_sdk  # noqa: E402

SDK_TOML = 'version = "{version}"\ncommit = "abc"\ndirty = false\nconfig = "Release"\nbuild_id = "b"\nplatform = "{os}"\n'


def make_tree(root, version, os_name, editor=True, top=None):
    base = Path(root) / top if top else Path(root)
    (base / "bin").mkdir(parents=True)
    (base / "sdk.toml").write_text(SDK_TOML.format(version=version, os=os_name), encoding="utf-8")
    if editor:
        (base / "bin" / ("wind_editor.exe" if os_name == "windows" else "wind_editor")).write_bytes(b"editor")
    (base / "include").mkdir()
    (base / "include" / "a.h").write_text("//", encoding="utf-8")


def pack(tree, archive):
    tree = Path(tree)
    if str(archive).endswith(".zip"):
        with zipfile.ZipFile(archive, "w") as bundle:
            for path in sorted(tree.rglob("*")):
                bundle.write(path, path.relative_to(tree).as_posix())
    else:
        with tarfile.open(archive, "w:gz") as bundle:
            for path in sorted(tree.iterdir()):
                bundle.add(path, arcname=path.name)


def release(directory, version, os_name, arch, editor=True, top=None, tree_version=None):
    """A fake release asset and its .sha256 in `directory`."""
    name = setup_sdk.artifact_name(version, os_name, arch)
    with tempfile.TemporaryDirectory() as scratch:
        make_tree(scratch, tree_version or version, os_name, editor, top)
        pack(scratch, Path(directory) / name)
    digest = hashlib.sha256((Path(directory) / name).read_bytes()).hexdigest()
    (Path(directory) / (name + ".sha256")).write_text("%s  %s\n" % (digest, name), encoding="utf-8")
    return name


def quiet(function, *args, **kwargs):
    with contextlib.redirect_stderr(io.StringIO()):
        return function(*args, **kwargs)


class Names(unittest.TestCase):
    def test_artifact_names_for_every_platform(self):
        self.assertEqual(setup_sdk.artifact_name("0.1.7a", "windows", "x86_64"), "wind-sdk-0.1.7a-windows-x86_64.zip")
        self.assertEqual(setup_sdk.artifact_name("0.1.7a", "linux", "x86_64"), "wind-sdk-0.1.7a-linux-x86_64.tar.gz")
        self.assertEqual(setup_sdk.artifact_name("0.2.0", "macos", "arm64"), "wind-sdk-0.2.0-macos-arm64.tar.gz")
        self.assertEqual(setup_sdk.artifact_name("0.2.6b", "linux", "arm64"), "wind-sdk-0.2.6b-linux-arm64.tar.gz")

    def test_urls(self):
        name = setup_sdk.artifact_name("0.1.7a", "windows", "x86_64")
        self.assertEqual(setup_sdk.release_url("wismh/wind-engine", "0.1.7a", name),
                         "https://github.com/wismh/wind-engine/releases/download/v0.1.7a/wind-sdk-0.1.7a-windows-x86_64.zip")
        self.assertEqual(setup_sdk.release_url("me/fork", "0.2.0", "x.sha256"),
                         "https://github.com/me/fork/releases/download/v0.2.0/x.sha256")

    def test_cache_key(self):
        self.assertEqual(setup_sdk.cache_key("0.1.7a", "linux", "x86_64"), "wind-sdk-0.1.7a-linux-x86_64")

    def test_platform_names(self):
        self.assertEqual(setup_sdk.detect_platform("Windows", "AMD64"), ("windows", "x86_64"))
        self.assertEqual(setup_sdk.detect_platform("Linux", "x86_64"), ("linux", "x86_64"))
        self.assertEqual(setup_sdk.detect_platform("Linux", "aarch64"), ("linux", "arm64"))
        self.assertEqual(setup_sdk.detect_platform("Darwin", "arm64"), ("macos", "arm64"))
        self.assertEqual(setup_sdk.detect_platform("Darwin", "x86_64"), ("macos", "x86_64"))
        with self.assertRaises(setup_sdk.SetupError):
            setup_sdk.detect_platform("FreeBSD", "amd64")
        with self.assertRaises(setup_sdk.SetupError):
            setup_sdk.detect_platform("Linux", "riscv64")


class Toml(unittest.TestCase):
    def test_reads_the_engine_key(self):
        text = '# a game\nname = "Tic Tac Toe"\nengine = "0.1.7a"   # the SDK\ntarget = "ttt"\n'
        self.assertEqual(setup_sdk.read_toml_string(text, "engine"), "0.1.7a")
        self.assertEqual(setup_sdk.read_toml_string(text, "name"), "Tic Tac Toe")
        self.assertEqual(setup_sdk.read_toml_string("engine = '0.2.0'\n", "engine"), "0.2.0")
        self.assertEqual(setup_sdk.read_toml_string("  engine   =   \"1.0.0\"\r\n", "engine"), "1.0.0")

    def test_ignores_what_is_not_the_key(self):
        self.assertIsNone(setup_sdk.read_toml_string('# engine = "0.1.0"\n', "engine"))
        self.assertIsNone(setup_sdk.read_toml_string('name = "engine"\n', "engine"))
        self.assertIsNone(setup_sdk.read_toml_string('[extra]\nengine = "0.1.0"\n', "engine"))
        self.assertIsNone(setup_sdk.read_toml_string('engine = 3\n', "engine"))
        self.assertIsNone(setup_sdk.read_toml_string('engine_version = "0.1.0"\n', "engine"))

    def test_project_engine(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(setup_sdk.SetupError):
                setup_sdk.project_engine(directory)
            (Path(directory) / "wind_project.toml").write_text('name = "x"\n', encoding="utf-8")
            with self.assertRaises(setup_sdk.SetupError):
                setup_sdk.project_engine(directory)
            (Path(directory) / "wind_project.toml").write_text('﻿name = "x"\nengine = "0.1.7a"\n', encoding="utf-8")
            self.assertEqual(setup_sdk.project_engine(directory), "0.1.7a")


class Checksum(unittest.TestCase):
    def test_accepts_the_right_digest_in_both_formats(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "a.zip"
            archive.write_bytes(b"content")
            digest = hashlib.sha256(b"content").hexdigest()
            sha = Path(directory) / "a.zip.sha256"
            sha.write_text("%s  a.zip\n" % digest, encoding="utf-8")
            setup_sdk.verify_sha256(archive, sha)
            sha.write_text(digest.upper() + "\n", encoding="utf-8")
            setup_sdk.verify_sha256(archive, sha)
            sha.write_text("%s *a.zip" % digest, encoding="utf-8")
            setup_sdk.verify_sha256(archive, sha)

    def test_refuses_another_digest_and_junk(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "a.zip"
            archive.write_bytes(b"content")
            sha = Path(directory) / "a.zip.sha256"
            sha.write_text("%s  a.zip\n" % hashlib.sha256(b"other").hexdigest(), encoding="utf-8")
            with self.assertRaisesRegex(setup_sdk.SetupError, "does not match its SHA-256"):
                setup_sdk.verify_sha256(archive, sha)
            for junk in ("", "not a digest\n", "abc123  a.zip\n"):
                sha.write_text(junk, encoding="utf-8")
                with self.assertRaises(setup_sdk.SetupError):
                    setup_sdk.verify_sha256(archive, sha)


class Install(unittest.TestCase):
    def check(self, os_name, arch="x86_64", top=None):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", os_name, arch, top=top)
            dest = Path(directory) / "work" / "sdk"
            path = quiet(setup_sdk.install, "0.1.7a", dest, os_name, arch, from_dir=assets)
            self.assertEqual(path, dest)
            self.assertTrue((dest / "sdk.toml").is_file())
            self.assertTrue((dest / "include" / "a.h").is_file())
            self.assertTrue(setup_sdk.editor_path(dest, os_name).is_file())
            self.assertEqual([child.name for child in dest.parent.iterdir()], ["sdk"], "no temporary directory is left")

    def test_zip_of_windows(self):
        self.check("windows")

    def test_tar_gz_of_linux(self):
        self.check("linux")

    def test_tar_gz_of_macos_arm64(self):
        self.check("macos", "arm64")

    def test_an_archive_with_a_top_directory(self):
        self.check("linux", top="wind-sdk-0.1.7a-linux-x86_64")

    def test_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "linux", "x86_64")
            dest = Path(directory) / "sdk"
            quiet(setup_sdk.install, "0.1.7a", dest, "linux", "x86_64", from_dir=assets)
            (dest / "marker").write_text("kept", encoding="utf-8")
            # Nothing is read from the assets again: the directory holding them is gone.
            quiet(setup_sdk.install, "0.1.7a", dest, "linux", "x86_64", from_dir=Path(directory) / "gone")
            self.assertEqual((dest / "marker").read_text(encoding="utf-8"), "kept")

    def test_installs_again_over_another_version(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "linux", "x86_64")
            dest = Path(directory) / "sdk"
            make_tree(dest, "0.1.6a", "linux")
            quiet(setup_sdk.install, "0.1.7a", dest, "linux", "x86_64", from_dir=assets)
            setup_sdk.check_sdk(dest, "0.1.7a", "linux")

    def test_refuses_a_wrong_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            name = release(assets, "0.1.7a", "linux", "x86_64")
            (assets / (name + ".sha256")).write_text("0" * 64 + "  " + name + "\n", encoding="utf-8")
            dest = Path(directory) / "sdk"
            with self.assertRaisesRegex(setup_sdk.SetupError, "SHA-256"):
                quiet(setup_sdk.install, "0.1.7a", dest, "linux", "x86_64", from_dir=assets)
            self.assertFalse(dest.exists())

    def test_refuses_an_sdk_of_another_version(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "windows", "x86_64", tree_version="0.1.6a")
            dest = Path(directory) / "sdk"
            with self.assertRaisesRegex(setup_sdk.SetupError, "is version 0.1.6a, not 0.1.7a"):
                quiet(setup_sdk.install, "0.1.7a", dest, "windows", "x86_64", from_dir=assets)
            self.assertFalse(dest.exists())

    def test_refuses_an_sdk_without_the_editor(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "linux", "x86_64", editor=False)
            with self.assertRaisesRegex(setup_sdk.SetupError, "wind_editor"):
                quiet(setup_sdk.install, "0.1.7a", Path(directory) / "sdk", "linux", "x86_64", from_dir=assets)

    def test_refuses_an_sdk_of_another_platform(self):
        with tempfile.TemporaryDirectory() as directory:
            with tempfile.TemporaryDirectory() as scratch:
                make_tree(scratch, "0.1.7a", "windows")
                with self.assertRaisesRegex(setup_sdk.SetupError, "is for windows, not linux"):
                    setup_sdk.check_sdk(scratch, "0.1.7a", "linux")

    def test_a_missing_asset_is_said(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(setup_sdk.SetupError, "is not in"):
                quiet(setup_sdk.install, "0.1.7a", Path(directory) / "sdk", "linux", "x86_64", from_dir=directory)

    def test_refuses_a_path_that_leaves_the_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "evil.zip"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("../outside.txt", "x")
            with self.assertRaisesRegex(setup_sdk.SetupError, "outside the SDK"):
                setup_sdk.extract(archive, Path(directory) / "into")
            self.assertFalse((Path(directory) / "outside.txt").exists())
            archive = Path(directory) / "evil.tar.gz"
            with tarfile.open(archive, "w:gz") as bundle:
                data = io.BytesIO(b"x")
                info = tarfile.TarInfo("../outside.txt")
                info.size = 1
                bundle.addfile(info, data)
            with self.assertRaisesRegex(setup_sdk.SetupError, "outside the SDK"):
                setup_sdk.extract(archive, Path(directory) / "into2")


def symbols_release(directory, version, os_name, arch, build_id="b", tree_version=None, top=None):
    """A fake symbols package and its .sha256 in `directory`."""
    name = setup_sdk.symbols_artifact_name(version, os_name, arch)
    with tempfile.TemporaryDirectory() as scratch:
        base = Path(scratch) / top if top else Path(scratch)
        (base / "bin").mkdir(parents=True)
        (base / "bin" / "engine.pdb").write_bytes(b"pdb")
        (base / "symbols.toml").write_text('version = "%s"\nbuild_id = "%s"\nplatform = "%s"\n'
                                           % (tree_version or version, build_id, os_name), encoding="utf-8")
        pack(scratch, Path(directory) / name)
    digest = hashlib.sha256((Path(directory) / name).read_bytes()).hexdigest()
    (Path(directory) / (name + ".sha256")).write_text("%s  %s\n" % (digest, name), encoding="utf-8")
    return name


class Symbols(unittest.TestCase):
    def test_names(self):
        self.assertEqual(setup_sdk.symbols_artifact_name("0.1.7a", "windows", "x86_64"),
                         "wind-sdk-symbols-0.1.7a-windows-x86_64.zip")
        self.assertEqual(setup_sdk.cache_key("0.1.7a", "windows", "x86_64", True), "wind-sdk-0.1.7a-windows-x86_64-symbols")
        self.assertEqual(setup_sdk.symbols_artifact_name("0.1.7a", "linux", "x86_64"),
                         "wind-sdk-symbols-0.1.7a-linux-x86_64.tar.gz")
        self.assertEqual(setup_sdk.cache_key("0.1.7a", "linux", "x86_64", True), "wind-sdk-0.1.7a-linux-x86_64-symbols")
        self.assertEqual(setup_sdk.cache_key("0.1.7a", "linux", "x86_64", True, True), "wind-sdk-0.1.7a-linux-x86_64-symbols-source")
        self.assertEqual(setup_sdk.source_artifact_name("0.1.7a"), "wind-sdk-source-0.1.7a.tar.gz")

    def test_unpacks_over_the_sdk_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "windows", "x86_64")
            symbols_release(assets, "0.1.7a", "windows", "x86_64", top="wind-sdk-symbols-0.1.7a-windows-x86_64")
            dest = Path(directory) / "work" / "sdk"
            quiet(setup_sdk.install, "0.1.7a", dest, "windows", "x86_64", from_dir=assets, symbols=True)
            self.assertTrue((dest / "bin" / "engine.pdb").is_file())
            self.assertTrue((dest / "bin" / "wind_editor.exe").is_file())
            self.assertEqual([child.name for child in dest.parent.iterdir()], ["sdk"], "no temporary directory is left")
            (dest / "bin" / "engine.pdb").unlink()
            quiet(setup_sdk.install, "0.1.7a", dest, "windows", "x86_64", from_dir=Path(directory) / "gone", symbols=True)
            self.assertFalse((dest / "bin" / "engine.pdb").exists(), "nothing is unpacked again")

    def test_adds_symbols_to_an_sdk_installed_without_them(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "windows", "x86_64")
            symbols_release(assets, "0.1.7a", "windows", "x86_64")
            dest = Path(directory) / "sdk"
            quiet(setup_sdk.install, "0.1.7a", dest, "windows", "x86_64", from_dir=assets)
            self.assertFalse((dest / "bin" / "engine.pdb").exists())
            quiet(setup_sdk.install, "0.1.7a", dest, "windows", "x86_64", from_dir=assets, symbols=True)
            self.assertTrue((dest / "bin" / "engine.pdb").is_file())

    def test_refuses_symbols_of_another_build(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "windows", "x86_64")
            symbols_release(assets, "0.1.7a", "windows", "x86_64", build_id="other")
            with self.assertRaisesRegex(setup_sdk.SetupError, "another build"):
                quiet(setup_sdk.install, "0.1.7a", Path(directory) / "sdk", "windows", "x86_64", from_dir=assets, symbols=True)

    def test_refuses_symbols_of_another_version(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "windows", "x86_64")
            symbols_release(assets, "0.1.7a", "windows", "x86_64", tree_version="0.1.6a")
            with self.assertRaisesRegex(setup_sdk.SetupError, "symbols are version 0.1.6a"):
                quiet(setup_sdk.install, "0.1.7a", Path(directory) / "sdk", "windows", "x86_64", from_dir=assets, symbols=True)

    def test_a_tar_gz_of_linux(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "linux", "x86_64")
            symbols_release(assets, "0.1.7a", "linux", "x86_64")
            dest = Path(directory) / "sdk"
            quiet(setup_sdk.install, "0.1.7a", dest, "linux", "x86_64", from_dir=assets, symbols=True)
            self.assertTrue((dest / "bin" / "engine.pdb").is_file())

    def test_a_missing_package_is_said(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "windows", "x86_64")
            with self.assertRaisesRegex(setup_sdk.SetupError, "wind-sdk-symbols-0.1.7a-windows-x86_64.zip is not in"):
                quiet(setup_sdk.install, "0.1.7a", Path(directory) / "sdk", "windows", "x86_64", from_dir=assets, symbols=True)


def source_release(directory, version, tree_version=None, top=None):
    name = setup_sdk.source_artifact_name(version)
    with tempfile.TemporaryDirectory() as scratch:
        base = Path(scratch) / top if top else Path(scratch)
        (base / "source" / "src").mkdir(parents=True)
        (base / "source" / "CMakeLists.txt").write_text("project(engine)", encoding="utf-8")
        (base / "source.toml").write_text('version = "%s"' % (tree_version or version), encoding="utf-8")
        pack(scratch, Path(directory) / name)
    digest = hashlib.sha256((Path(directory) / name).read_bytes()).hexdigest()
    (Path(directory) / (name + ".sha256")).write_text("%s  %s" % (digest, name), encoding="utf-8")


class Source(unittest.TestCase):
    def test_unpacks_as_the_source_directory_of_the_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "windows", "x86_64")
            source_release(assets, "0.1.7a", top="wind-sdk-source-0.1.7a")
            dest = Path(directory) / "work" / "sdk"
            quiet(setup_sdk.install, "0.1.7a", dest, "windows", "x86_64", from_dir=assets, source=True)
            self.assertTrue((dest / "source" / "CMakeLists.txt").is_file())
            self.assertTrue((dest / "bin" / "wind_editor.exe").is_file())
            self.assertEqual([child.name for child in dest.parent.iterdir()], ["sdk"], "no temporary directory is left")

    def test_is_idempotent_and_independent_of_the_platform_package(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "linux", "x86_64")
            source_release(assets, "0.1.7a")
            dest = Path(directory) / "sdk"
            quiet(setup_sdk.install, "0.1.7a", dest, "linux", "x86_64", from_dir=assets, source=True)
            (dest / "source" / "CMakeLists.txt").unlink()
            quiet(setup_sdk.install, "0.1.7a", dest, "linux", "x86_64", from_dir=Path(directory) / "gone", source=True)
            self.assertFalse((dest / "source" / "CMakeLists.txt").exists(), "nothing is unpacked again")

    def test_refuses_the_source_of_another_version(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "linux", "x86_64")
            source_release(assets, "0.1.7a", tree_version="0.1.6a")
            with self.assertRaisesRegex(setup_sdk.SetupError, "source are version 0.1.6a"):
                quiet(setup_sdk.install, "0.1.7a", Path(directory) / "sdk", "linux", "x86_64", from_dir=assets, source=True)

    def test_a_missing_package_is_said(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            release(assets, "0.1.7a", "linux", "x86_64")
            with self.assertRaisesRegex(setup_sdk.SetupError, "wind-sdk-source-0.1.7a.tar.gz is not in"):
                quiet(setup_sdk.install, "0.1.7a", Path(directory) / "sdk", "linux", "x86_64", from_dir=assets, source=True)


class CommandLine(unittest.TestCase):
    def run_main(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = setup_sdk.main(list(args))
        return code, out.getvalue()

    def test_from_dir_with_the_version_of_the_project(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "game").mkdir()
            (root / "game" / "wind_project.toml").write_text('name = "g"\nengine = "0.1.7a"\ntarget = "g"\n', encoding="utf-8")
            (root / "assets").mkdir()
            os_name, arch = setup_sdk.detect_platform()
            release(root / "assets", "0.1.7a", os_name, arch)
            code, out = self.run_main("--project", str(root / "game"), "--dest", str(root / "sdk"),
                                      "--from-dir", str(root / "assets"))
            self.assertEqual(code, 0)
            self.assertEqual(Path(out.strip().splitlines()[-1]), (root / "sdk").resolve())

    def test_an_unknown_version_text_is_refused(self):
        code, out = self.run_main("--version", "latest", "--dest", "unused")
        self.assertEqual(code, 1)
        self.assertEqual(out, "")

    def test_print_key(self):
        code, out = self.run_main("--version", "0.2.6b", "--os", "linux", "--arch", "arm64", "--print-key")
        self.assertEqual(code, 0)
        self.assertEqual(out.splitlines(), ["key=wind-sdk-0.2.6b-linux-arm64", "version=0.2.6b", "os=linux", "arch=arm64"])

    def test_a_mismatch_between_the_project_and_the_archive_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "assets").mkdir()
            os_name, arch = setup_sdk.detect_platform()
            release(root / "assets", "0.1.7a", os_name, arch, tree_version="0.1.8a")
            code, out = self.run_main("--version", "0.1.7a", "--dest", str(root / "sdk"), "--from-dir", str(root / "assets"))
            self.assertEqual(code, 1)
            self.assertFalse((root / "sdk").exists())


if __name__ == "__main__":
    unittest.main()
