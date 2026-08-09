#!/usr/bin/env python3
"""Splits the debug information off an installed Wind editor SDK into a symbols package tree.

  python3 split_symbols.py --os windows --sdk out/sdk --symbols out/symbols

After it, the SDK holds the code and the symbols tree holds what a debugger needs, laid out so that unpacking it over
the SDK puts everything back (tools/ci/setup_sdk.py --symbols):

  every platform  player/<flavour>/lib/*.lib|*.a   the static libraries with debug information: the symbols tree gets the
                                                   original, the SDK a stripped copy (only if stripping saves a megabyte and a fifth)
  windows         bin/*.pdb                        moved
  linux           bin/<binary>                     debug information to bin/<binary>.debug (gnu-debuglink), binary stripped
  macos           bin/<binary>                     bin/<binary>.dSYM, binary stripped and signed again

Writes <symbols>/symbols.toml (version, build_id, platform of the SDK's sdk.toml). Nothing to split: no symbols tree,
exit code 0. docs/tech/build/Release.md

Standard library only; Python 3.8 or newer.
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# A static library is split only if stripping it saves this much and a fifth of its size: a library with a little debug
# information (a Shipping one) or a small one stays whole, as a copy of it in the symbols package would cost more than the
# SDK saves.
MIN_SAVING = 1 << 20

STATIC_LIBRARY = {"windows": ".lib", "linux": ".a", "macos": ".a"}


class SplitError(Exception):
    """Something the caller can fix; the message says what."""


def log(text):
    print(text, file=sys.stderr, flush=True)


def read_toml_string(path, key):
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r'^%s\s*=\s*"([^"]*)"\s*$' % re.escape(key), line.strip())
        if match:
            return match.group(1)
    return None


def run(command):
    log("  " + " ".join(shlex.quote(str(part)) for part in command))
    result = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            universal_newlines=True)
    if result.returncode != 0:
        raise SplitError("%s failed (exit code %d):\n%s" % (command[0], result.returncode, result.stdout))
    return result.stdout


def default_strip_command(os_name):
    """The command that removes debug information from a static library, as a list; the file is appended."""
    if os_name == "windows":
        found = shutil.which("llvm-strip")
        if not found:
            candidate = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin" / "llvm-strip.exe"
            found = str(candidate) if candidate.is_file() else None
        if not found:
            raise SplitError("llvm-strip is not installed: it strips the debug information of the .lib files "
                             "(LLVM is on the GitHub Windows runners; pass --strip-command to use another tool).")
        return [found, "-g"]
    if os_name == "macos":
        return ["strip", "-S"]
    return ["strip", "--strip-debug"]


def split_static_libraries(os_name, sdk, symbols, strip_command):
    """Moves the debug information of player/*/lib/*.{lib,a} into the symbols tree. Returns the relative paths."""
    moved = []
    extension = STATIC_LIBRARY[os_name]
    for library in sorted((sdk / "player").glob("*/lib/*" + extension)):
        original_size = library.stat().st_size
        with tempfile.TemporaryDirectory(prefix=".wind-strip-") as scratch:
            stripped = Path(scratch) / library.name
            shutil.copyfile(str(library), str(stripped))
            run(strip_command + [stripped])
            saving = original_size - stripped.stat().st_size
            if saving < MIN_SAVING or saving * 5 < original_size:
                continue
            relative = library.relative_to(sdk)
            (symbols / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(library), str(symbols / relative))
            shutil.move(str(stripped), str(library))
        log("%s: %d MB -> %d MB" % (relative.as_posix(), original_size >> 20, library.stat().st_size >> 20))
        moved.append(relative)
    return moved


def move_pdb_files(sdk, symbols):
    moved = []
    for pdb in sorted((sdk / "bin").glob("*.pdb")):
        (symbols / "bin").mkdir(parents=True, exist_ok=True)
        shutil.move(str(pdb), str(symbols / "bin" / pdb.name))
        moved.append(Path("bin") / pdb.name)
    return moved


def is_native_binary(path):
    """An ELF or Mach-O file (thin or universal): the files of bin/ whose debug information is split off."""
    with open(path, "rb") as stream:
        magic = stream.read(4)
    return magic in (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def native_binaries(sdk):
    return [path for path in sorted((sdk / "bin").iterdir()) if path.is_file() and is_native_binary(path)]


def split_elf_binaries(sdk, symbols):
    moved = []
    for binary in native_binaries(sdk):
        debug = symbols / "bin" / (binary.name + ".debug")
        debug.parent.mkdir(parents=True, exist_ok=True)
        run(["objcopy", "--only-keep-debug", binary, debug])
        run(["strip", "--strip-debug", binary])
        run(["objcopy", "--add-gnu-debuglink=" + str(debug), binary])
        moved.append(Path("bin") / debug.name)
    return moved


def split_macho_binaries(sdk, symbols):
    moved = []
    for binary in native_binaries(sdk):
        dsym = symbols / "bin" / (binary.name + ".dSYM")
        dsym.parent.mkdir(parents=True, exist_ok=True)
        run(["dsymutil", binary, "-o", dsym])
        run(["strip", "-S", binary])
        # strip invalidates the signature the linker made, and an arm64 macOS refuses to run an unsigned binary.
        run(["codesign", "--force", "--sign", "-", binary])
        moved.append(Path("bin") / dsym.name)
    return moved


def split(os_name, sdk, symbols, strip_command=None):
    """Returns the relative paths now in the symbols tree (empty: nothing was split, no tree was made)."""
    sdk = Path(sdk)
    symbols = Path(symbols)
    manifest = sdk / "sdk.toml"
    if not manifest.is_file():
        raise SplitError("%s is not an SDK: no sdk.toml." % sdk)
    platform = read_toml_string(manifest, "platform")
    if platform != os_name:
        raise SplitError("The SDK in %s is for %s, not %s." % (sdk, platform, os_name))
    moved = []
    if os_name == "windows":
        moved += move_pdb_files(sdk, symbols)
    elif os_name == "linux":
        moved += split_elf_binaries(sdk, symbols)
    else:
        moved += split_macho_binaries(sdk, symbols)
    moved += split_static_libraries(os_name, sdk, symbols, strip_command or default_strip_command(os_name))
    if not moved:
        return []
    lines = ["# Wind SDK symbols. Unpack over the SDK of the same version and build."]
    for key in ("version", "build_id", "platform"):
        value = read_toml_string(manifest, key)
        if value is None:
            raise SplitError("sdk.toml has no %s." % key)
        lines.append('%s = "%s"' % (key, value))
    (symbols / "symbols.toml").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return moved


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--os", dest="os_name", required=True, choices=("windows", "linux", "macos"))
    parser.add_argument("--sdk", required=True, help="the installed SDK")
    parser.add_argument("--symbols", required=True, help="where the symbols tree is made")
    parser.add_argument("--strip-command", help="the tool that strips a static library (the file is appended), "
                                                "default: llvm-strip -g | strip -S | strip --strip-debug")
    args = parser.parse_args(argv)
    try:
        command = shlex.split(args.strip_command) if args.strip_command else None
        moved = split(args.os_name, args.sdk, args.symbols, command)
    except SplitError as problem:
        print("split_symbols: %s" % problem, file=sys.stderr)
        return 1
    if not moved:
        log("Nothing to split: the SDK has no debug information.")
    else:
        log("Split %d items into %s." % (len(moved), args.symbols))
    return 0


if __name__ == "__main__":
    sys.exit(main())
