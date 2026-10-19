#!/usr/bin/env python3
"""Prepare the Android build: SDL2's source (SHA-256 verified), its Java glue,
and the game's generated C.

  python android/tools/setup_deps.py [--recompiler PATH]

The recompiler is a host tool: it cannot run inside the NDK cross-build, so
the S3K C is emitted here, from the owner's ROM (game/sonic3k/sonic3k.bin),
into android/generated/sonic3k (gitignored, like all generated C). It is
emitted without --reverse-debug: the device build is a plain native build.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import tarfile
import urllib.request
from pathlib import Path

ANDROID_ROOT = Path(__file__).resolve().parents[1]
REPO = ANDROID_ROOT.parent
EXTERNAL = ANDROID_ROOT / "external"
GENERATED = ANDROID_ROOT / "generated" / "sonic3k"

SDL_VERSION = "2.28.5"
SDL_ARCHIVE = EXTERNAL / f"SDL2-{SDL_VERSION}.tar.gz"
SDL_DIR = EXTERNAL / f"SDL2-{SDL_VERSION}"
SDL_URL = ("https://github.com/libsdl-org/SDL/releases/download/"
           f"release-{SDL_VERSION}/SDL2-{SDL_VERSION}.tar.gz")
SDL_SHA256 = "332cb37d0be20cb9541739c61f79bae5a477427d79ae85e352089afdaf6666e4"
SDL_JAVA = ANDROID_ROOT / "app" / "src" / "main" / "java" / "org" / "libsdl" / "app"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_sdl() -> None:
    if (SDL_DIR / "CMakeLists.txt").is_file() and (SDL_DIR / "src" / "SDL.c").is_file():
        print(f"SDL2 {SDL_VERSION}: present")
    else:
        EXTERNAL.mkdir(parents=True, exist_ok=True)
        if not SDL_ARCHIVE.exists():
            print(f"SDL2 {SDL_VERSION}: downloading")
            partial = SDL_ARCHIVE.with_suffix(".part")
            with urllib.request.urlopen(SDL_URL) as response, partial.open("wb") as out:
                shutil.copyfileobj(response, out)
            partial.replace(SDL_ARCHIVE)
        if sha256(SDL_ARCHIVE) != SDL_SHA256:
            raise SystemExit(f"{SDL_ARCHIVE}: SHA-256 mismatch")
        root = EXTERNAL.resolve()
        with tarfile.open(SDL_ARCHIVE, "r:gz") as archive:
            for member in archive.getmembers():
                dest = (EXTERNAL / member.name).resolve()
                if dest != root and root not in dest.parents:
                    raise SystemExit(f"unsafe archive path: {member.name}")
            archive.extractall(EXTERNAL, filter="data") if hasattr(tarfile, "data_filter") \
                else archive.extractall(EXTERNAL)
        print(f"SDL2 {SDL_VERSION}: extracted")
    # NDK 27+ removed ALooper_pollAll (it may drop wakes); SDL 2.28 still calls
    # it. ALooper_pollOnce is the documented replacement, same arguments
    for c in (SDL_DIR / "src").rglob("*.c"):
        t = c.read_text(encoding="utf-8", errors="surrogateescape")
        if "ALooper_pollAll(" in t:
            c.write_text(t.replace("ALooper_pollAll(", "ALooper_pollOnce("), encoding="utf-8",
                         errors="surrogateescape")
            print(f"patched {c.relative_to(SDL_DIR)}: ALooper_pollOnce")
    # SDLActivity and friends: the Java half of SDL's Android backend, at the
    # same version as the C half
    src = SDL_DIR / "android-project" / "app" / "src" / "main" / "java" / "org" / "libsdl" / "app"
    SDL_JAVA.mkdir(parents=True, exist_ok=True)
    for f in src.glob("*.java"):
        shutil.copy2(f, SDL_JAVA / f.name)
    print(f"SDL2 Java glue -> {SDL_JAVA.relative_to(ANDROID_ROOT)}")


def generate(recompiler: Path) -> None:
    game = REPO / "game" / "sonic3k"
    if not (game / "sonic3k.bin").is_file():
        raise SystemExit("game/sonic3k/sonic3k.bin (your Sonic 3 & Knuckles lock-on dump) is needed")
    if not recompiler.is_file():
        raise SystemExit(f"{recompiler}: build the PC game first (cmake --build build --config Release)")
    stage = GENERATED.with_name("sonic3k.stage")
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    print("Generating the S3K C (a few minutes)…")
    subprocess.run([str(recompiler), "sonic3k.bin", "--game", "game.toml", "--output-dir", str(stage)],
                   cwd=game, check=True, stdout=subprocess.DEVNULL)
    shutil.rmtree(GENERATED, ignore_errors=True)
    stage.rename(GENERATED)
    print(f"generated C -> {GENERATED.relative_to(ANDROID_ROOT)}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--recompiler", default=str(REPO / "build" / "Release" / "GenesisRecomp.exe"))
    ap.add_argument("--no-generate", action="store_true")
    a = ap.parse_args()
    fetch_sdl()
    if not a.no_generate:
        generate(Path(a.recompiler))


if __name__ == "__main__":
    main()
