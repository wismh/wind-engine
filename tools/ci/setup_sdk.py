#!/usr/bin/env python3
"""Installs a released Wind editor SDK for a game's CI: download, SHA-256 check, unpack, sanity check.

  python3 setup_sdk.py --project . --dest "$RUNNER_TEMP/wind-sdk"
  python3 setup_sdk.py --version 0.1.7a --dest sdk
  python3 setup_sdk.py --version 0.1.7a --dest sdk --from-dir release-artifacts      no network
  python3 setup_sdk.py --version 0.1.7a --dest sdk --symbols                         also the debug symbols
  python3 setup_sdk.py --version 0.1.7a --dest sdk --source                          also <sdk>/source (Web, Android)

The version is --version, or the `engine` key of <--project>/wind_project.toml. The SDK is the release asset
wind-sdk-<version>-<os>-<arch>.<zip|tar.gz> (and its .sha256) of the tag v<version> in --repo. The last line printed
on stdout is the SDK directory; the progress goes to stderr. Run again with the same arguments and an SDK of that
version already in --dest, and nothing is downloaded. --symbols adds the separate package
wind-sdk-symbols-<version>-<os>-<arch>.<zip|tar.gz> (debug information split off the SDK) over the SDK; it must be of
the same build (`build_id` in its symbols.toml and in sdk.toml). --source adds wind-sdk-source-<version>.tar.gz (one
for every platform), which unpacks to <sdk>/source. docs/tech/build/Release.md, docs/manual/getting-started/Continuous-Integration.md

Standard library only; Python 3.8 or newer.
"""

import argparse
import hashlib
import os
import platform
import re
import shutil
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

DEFAULT_REPO = "wismh/wind-engine"
VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+[ab]?(-dev)?$")


class SetupError(Exception):
    """Something the caller can fix; the message says what."""


def log(text):
    print(text, file=sys.stderr, flush=True)


def detect_platform(system=None, machine=None):
    """(os, arch) of the machine, with the names the release assets use: windows|linux|macos, x86_64|arm64."""
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    systems = {"windows": "windows", "linux": "linux", "darwin": "macos"}
    arches = {"amd64": "x86_64", "x86_64": "x86_64", "x64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}
    if system.lower() not in systems:
        raise SetupError("There is no Wind SDK for the operating system %s." % system)
    if machine not in arches:
        raise SetupError("There is no Wind SDK for the processor %s." % machine)
    return systems[system.lower()], arches[machine]


def artifact_name(version, os_name, arch):
    extension = "zip" if os_name == "windows" else "tar.gz"
    return "wind-sdk-%s-%s-%s.%s" % (version, os_name, arch, extension)


def symbols_artifact_name(version, os_name, arch):
    return artifact_name(version, os_name, arch).replace("wind-sdk-", "wind-sdk-symbols-", 1)


def source_artifact_name(version):
    """The engine source is one package for every platform."""
    return "wind-sdk-source-%s.tar.gz" % version


def release_url(repo, version, name):
    return "https://github.com/%s/releases/download/v%s/%s" % (repo, version, name)


def cache_key(version, os_name, arch, symbols=False, source=False):
    return "wind-sdk-%s-%s-%s%s%s" % (version, os_name, arch, "-symbols" if symbols else "", "-source" if source else "")


def read_toml_string(text, key):
    """The value of `key = "..."` at the start of a line (basic or literal string), or None. Not a TOML parser: the two
    files read here (wind_project.toml, sdk.toml) are flat, and a key inside a table is not looked at."""
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("["):
            return None
        match = re.match(r'^%s\s*=\s*(?:"([^"\\]*)"|\'([^\']*)\')\s*(?:#.*)?$' % re.escape(key), stripped)
        if match:
            return match.group(1) if match.group(1) is not None else match.group(2)
    return None


def project_engine(project_dir):
    path = Path(project_dir) / "wind_project.toml"
    try:
        text = path.read_text(encoding="utf-8-sig")
    except OSError as error:
        raise SetupError("Cannot read %s: %s" % (path, error.strerror or error))
    engine = read_toml_string(text, "engine")
    if engine is None:
        raise SetupError('%s has no engine = "..." line.' % path)
    return engine


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def expected_sha256(text):
    """The digest in a .sha256 file: `<hex>  <name>` (sha256sum), or the hex alone."""
    words = text.split()
    if not words or not re.fullmatch(r"[0-9a-fA-F]{64}", words[0]):
        raise SetupError("The .sha256 file does not start with a SHA-256 digest.")
    return words[0].lower()


def verify_sha256(archive, sha_file):
    expected = expected_sha256(Path(sha_file).read_text(encoding="utf-8-sig"))
    actual = sha256_of(archive)
    if actual != expected:
        raise SetupError("%s does not match its SHA-256 (expected %s, got %s)." % (Path(archive).name, expected, actual))


class _DropCredentialsOnRedirect(urllib.request.HTTPRedirectHandler):
    """The release asset redirects to another host (object storage), which must not get the GitHub token."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and urllib.parse.urlsplit(newurl).hostname != urllib.parse.urlsplit(req.full_url).hostname:
            new.remove_header("Authorization")
        return new


def download(url, destination, token=None, attempts=3):
    opener = urllib.request.build_opener(_DropCredentialsOnRedirect)
    request = urllib.request.Request(url, headers={"User-Agent": "wind-setup-sdk"})
    if token:
        request.add_header("Authorization", "Bearer " + token)
    for attempt in range(1, attempts + 1):
        try:
            with opener.open(request, timeout=60) as response, open(destination, "wb") as out:
                shutil.copyfileobj(response, out)
            return
        except urllib.error.HTTPError as error:
            if error.code == 404:
                raise SetupError("Not found: %s. Is there a release of that version for this platform?" % url)
            failure = "HTTP %d" % error.code
        except (urllib.error.URLError, OSError) as error:
            failure = str(error)
        if attempt == attempts:
            raise SetupError("Could not download %s: %s" % (url, failure))
        log("Download failed (%s), trying again." % failure)
        time.sleep(2 * attempt)


def _inside(root, member):
    root = os.path.realpath(root)
    target = os.path.realpath(os.path.join(root, member))
    return target == root or target.startswith(root + os.sep)


def extract(archive, into):
    archive = str(archive)
    os.makedirs(into, exist_ok=True)
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as bundle:
            for name in bundle.namelist():
                if not _inside(into, name):
                    raise SetupError("%s holds a path outside the SDK: %s" % (Path(archive).name, name))
            bundle.extractall(into)
    elif archive.endswith(".tar.gz"):
        with tarfile.open(archive, "r:gz") as bundle:
            for member in bundle.getmembers():
                if not _inside(into, member.name) or member.name.startswith("/"):
                    raise SetupError("%s holds a path outside the SDK: %s" % (Path(archive).name, member.name))
                if member.islnk() or member.issym():
                    link = os.path.join(os.path.dirname(member.name), member.linkname) if member.issym() else member.linkname
                    if not _inside(into, link):
                        raise SetupError("%s holds a link outside the SDK: %s" % (Path(archive).name, member.name))
            if hasattr(tarfile, "data_filter"):
                bundle.extractall(into, filter="data")
            else:
                bundle.extractall(into)
    else:
        raise SetupError("Unknown archive type: %s" % archive)


def find_sdk_root(directory):
    """The SDK root in an unpacked archive: the directory itself, or its only subdirectory."""
    directory = Path(directory)
    if (directory / "sdk.toml").is_file():
        return directory
    children = [child for child in directory.iterdir() if child.is_dir()]
    if len(children) == 1 and (children[0] / "sdk.toml").is_file():
        return children[0]
    raise SetupError("The archive holds no sdk.toml at its top.")


def editor_path(sdk_dir, os_name):
    return Path(sdk_dir) / "bin" / ("wind_editor.exe" if os_name == "windows" else "wind_editor")


def check_sdk(sdk_dir, version, os_name):
    """Raises SetupError unless sdk_dir is the Wind SDK of `version` for `os_name`."""
    try:
        text = (Path(sdk_dir) / "sdk.toml").read_text(encoding="utf-8-sig")
    except OSError as error:
        raise SetupError("%s has no readable sdk.toml: %s" % (sdk_dir, error.strerror or error))
    found = read_toml_string(text, "version")
    if found != version:
        raise SetupError("The SDK in %s is version %s, not %s." % (sdk_dir, found, version))
    found_platform = read_toml_string(text, "platform")
    if found_platform is not None and found_platform != os_name:
        raise SetupError("The SDK in %s is for %s, not %s." % (sdk_dir, found_platform, os_name))
    if not editor_path(sdk_dir, os_name).is_file():
        raise SetupError("The SDK in %s has no %s." % (sdk_dir, editor_path(sdk_dir, os_name).name))


def fetch(name, work, repo, version, from_dir, token):
    """The release asset `name` and its .sha256 in `work`, checked. Returns the archive path."""
    archive = work / name
    sha_file = work / (name + ".sha256")
    for file in (archive, sha_file):
        if from_dir is not None:
            source = Path(from_dir) / file.name
            if not source.is_file():
                raise SetupError("%s is not in %s." % (file.name, from_dir))
            shutil.copyfile(str(source), str(file))
        else:
            url = release_url(repo, version, file.name)
            log("Downloading %s" % url)
            download(url, str(file), token)
    verify_sha256(archive, sha_file)
    return archive


def find_package_root(directory, marker):
    """The root of an unpacked package: the directory itself, or its only subdirectory (the one with `marker`)."""
    directory = Path(directory)
    if (directory / marker).is_file():
        return directory
    children = [child for child in directory.iterdir() if child.is_dir()]
    if len(children) == 1 and (children[0] / marker).is_file():
        return children[0]
    raise SetupError("The archive holds no %s at its top." % marker)


def _read_key(path, key):
    try:
        return read_toml_string(Path(path).read_text(encoding="utf-8-sig"), key)
    except OSError:
        return None


def symbols_installed(sdk_dir, version):
    """Whether the symbols of this SDK's build are already over it."""
    marker = Path(sdk_dir) / "symbols.toml"
    build_id = _read_key(Path(sdk_dir) / "sdk.toml", "build_id")
    return (marker.is_file() and _read_key(marker, "version") == version and build_id is not None
            and _read_key(marker, "build_id") == build_id)


def _installed(sdk_dir, version, marker, same_build):
    """Whether the package with the marker file `marker` of this version (and build) is already over the SDK."""
    found = Path(sdk_dir) / marker
    if not found.is_file() or _read_key(found, "version") != version:
        return False
    if not same_build:
        return True
    build_id = _read_key(Path(sdk_dir) / "sdk.toml", "build_id")
    return build_id is not None and _read_key(found, "build_id") == build_id


def symbols_installed(sdk_dir, version):
    return _installed(sdk_dir, version, "symbols.toml", True)


def _install_package(what, name, marker, same_build, version, sdk_dir, repo, from_dir, token):
    """Unpacks the release package `name` over the SDK in `sdk_dir` after checking its marker file."""
    sdk_dir = Path(sdk_dir)
    if _installed(sdk_dir, version, marker, same_build):
        log("The %s of the Wind SDK %s are already in %s." % (what, version, sdk_dir))
        return
    work = Path(tempfile.mkdtemp(prefix=".wind-%s-" % what, dir=str(sdk_dir.parent)))
    try:
        archive = fetch(name, work, repo, version, from_dir, token)
        unpacked = work / "unpacked"
        extract(archive, unpacked)
        root = find_package_root(unpacked, marker)
        found = _read_key(root / marker, "version")
        if found != version:
            raise SetupError("The %s are version %s, not %s." % (what, found, version))
        if same_build and _read_key(root / marker, "build_id") != _read_key(sdk_dir / "sdk.toml", "build_id"):
            raise SetupError("The %s are of another build than the SDK in %s." % (what, sdk_dir))
        shutil.copytree(str(root), str(sdk_dir), dirs_exist_ok=True)
    finally:
        shutil.rmtree(str(work), ignore_errors=True)
    log("Installed the %s of the Wind SDK %s in %s." % (what, version, sdk_dir))


def install_symbols(version, sdk_dir, os_name, arch, repo=DEFAULT_REPO, from_dir=None, token=None):
    """Unpacks the symbols package over the SDK: the debug information split off by tools/ci/split_symbols.py."""
    _install_package("symbols", symbols_artifact_name(version, os_name, arch), "symbols.toml", True,
                     version, sdk_dir, repo, from_dir, token)


def install_source(version, sdk_dir, repo=DEFAULT_REPO, from_dir=None, token=None):
    """Unpacks the engine source package over the SDK, as <sdk>/source: what a Web or Android build compiles."""
    _install_package("source", source_artifact_name(version), "source.toml", False,
                     version, sdk_dir, repo, from_dir, token)


def install(version, dest, os_name, arch, repo=DEFAULT_REPO, from_dir=None, token=None, symbols=False, source=False):
    """Makes `dest` the SDK of `version`, with its symbols package when `symbols` and the engine source when `source`.
    Returns the path; downloads nothing when it already is."""
    dest = Path(dest)
    install_sdk(version, dest, os_name, arch, repo, from_dir, token)
    if symbols:
        install_symbols(version, dest, os_name, arch, repo, from_dir, token)
    if source:
        install_source(version, dest, repo, from_dir, token)
    return dest


def install_sdk(version, dest, os_name, arch, repo, from_dir, token):
    if dest.is_dir():
        try:
            check_sdk(dest, version, os_name)
            log("The Wind SDK %s is already in %s." % (version, dest))
            return
        except SetupError as problem:
            log("%s Installing again." % problem)
    name = artifact_name(version, os_name, arch)
    dest.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=".wind-sdk-", dir=str(dest.parent)))
    try:
        archive = fetch(name, work, repo, version, from_dir, token)
        unpacked = work / "unpacked"
        extract(archive, unpacked)
        root = find_sdk_root(unpacked)
        check_sdk(root, version, os_name)
        if dest.exists():
            shutil.rmtree(str(dest))
        shutil.move(str(root), str(dest))
    finally:
        shutil.rmtree(str(work), ignore_errors=True)
    log("Installed the Wind SDK %s in %s." % (version, dest))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", help="the SDK version, e.g. 0.1.7a; default: engine in the project's wind_project.toml")
    parser.add_argument("--project", default=".", help="the directory of wind_project.toml (default: .)")
    parser.add_argument("--repo", default=DEFAULT_REPO, help="owner/name of the repository with the releases")
    parser.add_argument("--dest", help="where the SDK goes (default: .wind-sdk/<version>)")
    parser.add_argument("--from-dir", help="a directory with the release assets, instead of the download")
    parser.add_argument("--symbols", action="store_true", help="also the debug symbols package")
    parser.add_argument("--source", action="store_true", help="also the engine source (<sdk>/source, for Web and Android)")
    parser.add_argument("--print-key", action="store_true", help="print the cache key, version, and platform, and stop")
    parser.add_argument("--os", dest="os_name", choices=("windows", "linux", "macos"), help="default: this machine")
    parser.add_argument("--arch", choices=("x86_64", "arm64"), help="default: this machine")
    args = parser.parse_args(argv)
    try:
        version = args.version or project_engine(args.project)
        if not VERSION.match(version):
            raise SetupError('"%s" is not an engine version (like 0.1.7a).' % version)
        detected_os, detected_arch = detect_platform()
        os_name = args.os_name or detected_os
        arch = args.arch or detected_arch
        if args.print_key:
            print("key=%s" % cache_key(version, os_name, arch, args.symbols, args.source))
            print("version=%s" % version)
            print("os=%s" % os_name)
            print("arch=%s" % arch)
            return 0
        dest = Path(args.dest) if args.dest else Path(".wind-sdk") / version
        token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
        path = install(version, dest, os_name, arch, args.repo, args.from_dir, token, args.symbols, args.source)
    except SetupError as problem:
        print("setup_sdk: %s" % problem, file=sys.stderr)
        return 1
    print(str(path.resolve()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
