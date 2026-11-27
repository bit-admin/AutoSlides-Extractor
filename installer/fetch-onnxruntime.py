#!/usr/bin/env python3
"""Fetch the pinned ONNX Runtime from nuget.org into vendor/onnxruntime-<rid>-<version>/.

    python3 installer/fetch-onnxruntime.py --rid osx-arm64|linux-x64|win-x64 [--vendor DIR]
    python3 installer/fetch-onnxruntime.py --print-env    # ONNXRUNTIME_VERSION=<version>

The version and SHA-512 come from installer/onnxruntime.json. The package is
rejected unless its SHA-512 matches the pin. The result has the same layout as
the former GitHub release archives, which CMakeLists.txt and the packaging
scripts expect:

    include/                     C/C++ API headers
    lib/                         runtime libraries (CPU EP, plus Core ML on macOS)
      macOS:   libonnxruntime.<v>.dylib (its install name) + libonnxruntime.dylib link
      Linux:   libonnxruntime.so.<v> + libonnxruntime.so.1 (its SONAME) and .so links
      Windows: onnxruntime.dll/.lib, onnxruntime_providers_shared.dll/.lib
    LICENSE, ThirdPartyNotices.txt

Only the Python standard library is used.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PIN = ROOT / "installer" / "onnxruntime.json"
RIDS = ("osx-arm64", "linux-x64", "win-x64")


def load_pin():
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    for key in ("package", "version", "sha512"):
        if not pin.get(key):
            sys.exit(f"error: {PIN} has no {key}")
    return pin


def download(url, dest):
    for attempt in range(1, 4):
        try:
            with urllib.request.urlopen(url, timeout=120) as response, open(dest, "wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
            return
        except OSError as error:
            if attempt == 3:
                sys.exit(f"error: download failed: {url}: {error}")
            print(f"retrying after: {error}", flush=True)
            time.sleep(5 * attempt)


def sha512_b64(path):
    digest = hashlib.sha512()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return base64.b64encode(digest.digest()).decode()


def extract(nupkg, rid, version, target):
    native = f"runtimes/{rid}/native/"
    with zipfile.ZipFile(nupkg) as z:
        names = z.namelist()
        headers = [n for n in names if n.startswith("build/native/include/") and not n.endswith("/")]
        libs = [n for n in names if n.startswith(native) and not n.endswith("/")
                # Azure EP static lib (~22 MB) is not used by the app.
                and "azure" not in n.rsplit("/", 1)[-1]]
        if not headers or not libs:
            sys.exit(f"error: package has no headers or no {native} libraries")
        (target / "include").mkdir(parents=True)
        (target / "lib").mkdir()
        for name in headers:
            (target / "include" / name.rsplit("/", 1)[-1]).write_bytes(z.read(name))
        for name in libs:
            base = name.rsplit("/", 1)[-1]
            if rid == "osx-arm64" and base == "libonnxruntime.dylib":
                base = f"libonnxruntime.{version}.dylib"      # = its @rpath install name
            elif rid == "linux-x64" and base == "libonnxruntime.so":
                base = f"libonnxruntime.so.{version}"
            path = target / "lib" / base
            path.write_bytes(z.read(name))
            if not base.endswith((".dll", ".lib")):
                path.chmod(0o755)
        for name in ("LICENSE", "ThirdPartyNotices.txt"):
            if name in names:
                (target / name).write_bytes(z.read(name))
    lib = target / "lib"
    if rid == "osx-arm64":
        os.symlink(f"libonnxruntime.{version}.dylib", lib / "libonnxruntime.dylib")
    elif rid == "linux-x64":
        os.symlink(f"libonnxruntime.so.{version}", lib / "libonnxruntime.so.1")   # SONAME
        os.symlink("libonnxruntime.so.1", lib / "libonnxruntime.so")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rid", choices=RIDS, help="NuGet runtime identifier to extract")
    parser.add_argument("--vendor", type=Path, default=ROOT / "vendor", help="vendor directory (default: <repo>/vendor)")
    parser.add_argument("--print-env", action="store_true", help="print ONNXRUNTIME_VERSION=<pinned version> and exit")
    args = parser.parse_args()
    pin = load_pin()
    version = pin["version"]
    if args.print_env:
        print(f"ONNXRUNTIME_VERSION={version}")
        return
    if not args.rid:
        parser.error("--rid is required")

    target = args.vendor / f"onnxruntime-{args.rid}-{version}"
    package = pin["package"].lower()
    url = f"https://api.nuget.org/v3-flatcontainer/{package}/{version}/{package}.{version}.nupkg"
    args.vendor.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.vendor) as tmp:
        nupkg = Path(tmp) / "package.nupkg"
        print(f"Downloading {url}", flush=True)
        download(url, nupkg)
        got = sha512_b64(nupkg)
        if got != pin["sha512"]:
            sys.exit(f"error: SHA-512 mismatch for {pin['package']} {version}\n  expected {pin['sha512']}\n  got      {got}")
        print(f"SHA-512 OK ({nupkg.stat().st_size} bytes)")
        staged = Path(tmp) / target.name
        extract(nupkg, args.rid, version, staged)
        if target.is_symlink() or target.is_file():
            target.unlink()
        elif target.exists():
            shutil.rmtree(target)
        staged.rename(target)
    for path in sorted((target / "lib").iterdir()):
        size = "-> " + os.readlink(path) if path.is_symlink() else f"{path.stat().st_size / 1048576:8.2f} MB"
        print(f"  lib/{path.name:42} {size}")
    print(f"ONNX Runtime {version} ({args.rid}) in {target}")


if __name__ == "__main__":
    main()
